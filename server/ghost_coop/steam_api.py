"""Small Windows x64 binding to the official Steamworks flat C API.

Uses ISteamNetworkingMessages v002 (SDR), not the deprecated ISteamNetworking.
ABI reference: Steamworks SDK steam_api_flat.h / steamnetworkingtypes.h.
The DLL belongs to the standalone bridge process, never to Atomic Heart.
"""
from __future__ import annotations

import ctypes as C
import os
import time
from pathlib import Path

from .protocol import MAX_PACKET, MAX_PLAYERS
from .steam_client import SteamClient

APP_ID = 480
CHANNEL = 73
U64, I32, PTR = C.c_uint64, C.c_int32, C.c_void_p


class Identity(C.Structure):
    _pack_ = 1
    _fields_ = [("kind", I32), ("size", I32), ("data", C.c_ubyte * 128)]


class Message(C.Structure):
    _fields_ = [("data", PTR), ("size", I32), ("connection", C.c_uint32),
                ("peer", Identity), ("connection_data", C.c_int64),
                ("received", C.c_int64), ("number", C.c_int64),
                ("free_data", PTR), ("release", PTR), ("channel", I32),
                ("flags", I32), ("user_data", C.c_int64),
                ("lane", C.c_uint16), ("padding", C.c_uint16)]


class LobbyCreated(C.Structure):
    _fields_ = [("result", I32), ("lobby", U64)]


class LobbyEntered(C.Structure):
    _fields_ = [("lobby", U64), ("permissions", C.c_uint32),
                ("locked", C.c_bool), ("response", C.c_uint32)]


class Callback(C.Structure):
    _fields_ = [("user", I32), ("event", I32), ("data", PTR), ("size", I32)]


class AsyncCall(C.Structure):
    _fields_ = [("handle", U64), ("event", I32), ("size", C.c_uint32)]


class SteamAPI:
    def __init__(self, dll_path: str = ""):
        if os.name != "nt" or C.sizeof(PTR) != 8:
            raise RuntimeError("Steam bridge requires 64-bit Windows Python")
        path = Path(dll_path) if dll_path else Path(__file__).resolve().parents[2] / "runtime/steam/steam_api64.dll"
        if not path.is_file():
            raise RuntimeError("Steamworks steam_api64.dll missing; run tools/setup_steam.ps1 "
                               "-SteamworksSdk <SDK directory>, or pass -SteamApiPath <DLL>")
        self.initialized = False
        self.client = SteamClient()
        try:
            self.dll = C.CDLL(str(path.resolve()))
            self.client.route_sdk(self.dll)
            self._shutdown = self.bind("SteamAPI_Shutdown", None)
            self._pump = self.bind("SteamAPI_RunCallbacks", None)
            if hasattr(self.dll, "SteamAPI_InitFlat"):
                error = C.create_string_buffer(1024)
                result = self.bind("SteamAPI_InitFlat", I32, PTR)(error)
                if result != 0:
                    raise RuntimeError("SteamAPI_InitFlat failed: " + error.value.decode("utf-8", "replace") +
                                       " Start Steam and sign in under the same Windows user.")
            elif not self.bind("SteamAPI_InitSafe", C.c_bool)():
                raise RuntimeError("Steam initialization failed: start Steam and sign in (Spacewar AppID 480)")
            self.initialized = True
            self.user = self.interface("SteamUser", (23, 22, 21))
            self.utils = self.interface("SteamUtils", (11, 10, 9))
            self.match = self.interface("SteamMatchmaking", (9,))
            self.friends_api = self.interface("SteamFriends", (18, 17))
            self.net = self.interface("SteamNetworkingMessages_SteamAPI", (2,))
            self.net_utils = self.interface("SteamNetworkingUtils_SteamAPI", (4,))
            actual_app = self.bind("SteamAPI_ISteamUtils_GetAppID", C.c_uint32, PTR)(self.utils)
            if actual_app != APP_ID:
                raise RuntimeError(f"Steam initialized under AppID {actual_app}; expected Spacewar 480")
            if not self.bind("SteamAPI_ISteamUser_BLoggedOn", C.c_bool, PTR)(self.user):
                raise RuntimeError("Steam is offline; sign in before starting the bridge")
            self.steam_id = self.bind("SteamAPI_ISteamUser_GetSteamID", U64, PTR)(self.user)
            self._completed = self.bind("SteamAPI_ISteamUtils_IsAPICallCompleted", C.c_bool,
                                        PTR, U64, C.POINTER(C.c_bool))
            self._result = self.bind("SteamAPI_ISteamUtils_GetAPICallResult", C.c_bool,
                                     PTR, U64, PTR, I32, I32, C.POINTER(C.c_bool))
            self.bind("SteamAPI_ManualDispatch_Init", None)()
            self.pipe = self.bind("SteamAPI_GetHSteamPipe", I32)()
            self._frame = self.bind("SteamAPI_ManualDispatch_RunFrame", None, I32)
            self._next = self.bind("SteamAPI_ManualDispatch_GetNextCallback", C.c_bool, I32, C.POINTER(Callback))
            self._free = self.bind("SteamAPI_ManualDispatch_FreeLastCallback", None, I32)
            self._call_result = self.bind("SteamAPI_ManualDispatch_GetAPICallResult", C.c_bool,
                                          I32, U64, PTR, I32, I32, C.POINTER(C.c_bool))
            self.results, self.invites = {}, []
            self._set_identity = self.bind("SteamAPI_SteamNetworkingIdentity_SetSteamID64", None,
                                           C.POINTER(Identity), U64)
            self._get_identity = self.bind("SteamAPI_SteamNetworkingIdentity_GetSteamID64", U64,
                                           C.POINTER(Identity))
            self._send = self.bind("SteamAPI_ISteamNetworkingMessages_SendMessageToUser", I32,
                                   PTR, C.POINTER(Identity), PTR, C.c_uint32, I32, I32)
            self._receive = self.bind("SteamAPI_ISteamNetworkingMessages_ReceiveMessagesOnChannel", I32,
                                      PTR, I32, C.POINTER(C.POINTER(Message)), I32)
            self._release = self.bind("SteamAPI_SteamNetworkingMessage_t_Release", None, C.POINTER(Message))
            self._accept = self.bind("SteamAPI_ISteamNetworkingMessages_AcceptSessionWithUser", C.c_bool,
                                     PTR, C.POINTER(Identity))
            self._close = self.bind("SteamAPI_ISteamNetworkingMessages_CloseSessionWithUser", C.c_bool,
                                    PTR, C.POINTER(Identity))
            self.bind("SteamAPI_ISteamNetworkingUtils_InitRelayNetworkAccess", None, PTR)(self.net_utils)
        except BaseException:
            self.close()
            raise

    def bind(self, name, result, *arguments):
        try:
            function = getattr(self.dll, name)
        except AttributeError as error:
            raise RuntimeError(f"Steamworks DLL is too old or incompatible: missing {name}") from error
        function.restype, function.argtypes = result, list(arguments)
        return function

    def interface(self, name, versions):
        for version in versions:
            symbol = f"SteamAPI_{name}_v{version:03d}"
            if hasattr(self.dll, symbol):
                value = self.bind(symbol, PTR)()
                if value:
                    return value
        raise RuntimeError(f"Steamworks DLL does not support {name}")

    def call(self, method, result, arguments, *values):
        return self.bind("SteamAPI_ISteamMatchmaking_" + method, result, PTR, *arguments)(self.match, *values)

    def wait(self, handle, structure, callback, stop):
        if not handle:
            raise RuntimeError("Steam rejected lobby request")
        deadline = time.monotonic() + 30
        while not stop.is_set() and time.monotonic() < deadline:
            self.pump()
            if handle in self.results:
                event, data = self.results.pop(handle)
                if event != callback or data is None or len(data) != C.sizeof(structure):
                    raise RuntimeError("Steam lobby request failed")
                return structure.from_buffer_copy(data)
            stop.wait(0.02)
        raise RuntimeError("Steam lobby request cancelled" if stop.is_set() else "Steam lobby request timed out")

    def create_lobby(self, stop, lobby_type=3):
        # Invisible, joinable by lobby ID. Keep admission closed until metadata is ready.
        handle = self.call("CreateLobby", U64, (I32, I32), lobby_type, MAX_PLAYERS)
        result = self.wait(handle, LobbyCreated, 513, stop)
        if result.result != 1:
            raise RuntimeError(f"Steam could not create lobby (EResult {result.result})")
        self.call("SetLobbyJoinable", C.c_bool, (U64, C.c_bool), result.lobby, False)
        return result.lobby

    def join_lobby(self, lobby, stop):
        if not 0 < lobby < 2**64:
            raise ValueError("invalid Steam lobby ID")
        result = self.wait(self.call("JoinLobby", U64, (U64,), lobby), LobbyEntered, 504, stop)
        if result.response != 1:
            reason = {2: "lobby does not exist", 3: "access denied", 4: "room is full (8 players)",
                      5: "Steam error", 11: "rate limited"}.get(result.response, "join rejected")
            raise RuntimeError(f"{reason} (Steam response {result.response})")
        return result.lobby

    def set_data(self, lobby, key, value):
        if not self.call("SetLobbyData", C.c_bool, (U64, C.c_char_p, C.c_char_p),
                         lobby, key.encode(), value.encode()):
            raise RuntimeError("Steam could not publish lobby metadata")

    def open_lobby(self, lobby):
        if not self.call("SetLobbyJoinable", C.c_bool, (U64, C.c_bool), lobby, True):
            raise RuntimeError("Steam could not open lobby")

    def data(self, lobby, key):
        return (self.call("GetLobbyData", C.c_char_p, (U64, C.c_char_p), lobby, key.encode()) or b"").decode("utf-8", "replace")

    def set_member_session(self, lobby, session):
        self.call("SetLobbyMemberData", None, (U64, C.c_char_p, C.c_char_p), lobby, b"ahg_session", session.encode())

    def member_session(self, lobby, peer):
        return (self.call("GetLobbyMemberData", C.c_char_p, (U64, U64, C.c_char_p),
                          lobby, peer, b"ahg_session") or b"").decode("ascii", "replace")

    def members(self, lobby):
        count = self.call("GetNumLobbyMembers", I32, (U64,), lobby)
        if not 1 <= count <= MAX_PLAYERS:
            raise RuntimeError("Steam lobby membership unavailable or exceeds eight players")
        members = {self.call("GetLobbyMemberByIndex", U64, (U64, I32), lobby, i) for i in range(count)}
        if self.steam_id not in members or 0 in members:
            raise RuntimeError("Disconnected from Steam lobby")
        return members

    def identity(self, peer):
        identity = Identity()
        self._set_identity(C.byref(identity), peer)
        return identity

    def accept(self, peer):
        self._accept(self.net, C.byref(self.identity(peer)))

    def send(self, peer, payload):
        if len(payload) > MAX_PACKET:
            raise ValueError("Steam packet too large")
        # UnreliableNoNagle | AutoRestartBrokenSession: current snapshots supersede old ones.
        return self._send(self.net, C.byref(self.identity(peer)), payload, len(payload), 1 | 32, CHANNEL)

    def receive(self):
        slots = (C.POINTER(Message) * 64)()
        count = self._receive(self.net, CHANNEL, slots, len(slots))
        if not 0 <= count <= len(slots):
            raise RuntimeError("Steam receive failed")
        result = []
        for index in range(count):
            pointer = slots[index]
            try:
                message = pointer.contents
                if message.data and 0 < message.size <= MAX_PACKET and message.channel == CHANNEL:
                    peer = self._get_identity(C.byref(message.peer))
                    if peer:
                        result.append((peer, C.string_at(message.data, message.size)))
            finally:
                self._release(pointer)
        return result

    def close_peer(self, peer):
        self._close(self.net, C.byref(self.identity(peer)))

    def pump(self):
        self._frame(self.pipe)
        callback = Callback()
        for _ in range(256):
            if not self._next(self.pipe, C.byref(callback)):
                break
            try:
                if callback.event == 703 and callback.size == C.sizeof(AsyncCall):
                    result = C.cast(callback.data, C.POINTER(AsyncCall)).contents
                    if result.size <= 4096:
                        buffer, failed = C.create_string_buffer(result.size), C.c_bool()
                        ok = self._call_result(self.pipe, result.handle, buffer, result.size,
                                               result.event, C.byref(failed))
                        if len(self.results) >= 64:
                            self.results.pop(next(iter(self.results)))
                        self.results[result.handle] = (result.event, buffer.raw if ok and not failed.value else None)
                elif callback.event == 333 and callback.size == 16:
                    lobby = C.cast(callback.data, C.POINTER(U64)).contents.value
                    self.invites = (self.invites + [lobby])[-8:]
            finally:
                self._free(self.pipe)

    def friends(self):
        count = self.bind("SteamAPI_ISteamFriends_GetFriendCount", I32, PTR, I32)(self.friends_api, 4)
        index = self.bind("SteamAPI_ISteamFriends_GetFriendByIndex", U64, PTR, I32, I32)
        name = self.bind("SteamAPI_ISteamFriends_GetFriendPersonaName", C.c_char_p, PTR, U64)
        state = self.bind("SteamAPI_ISteamFriends_GetFriendPersonaState", I32, PTR, U64)
        result = []
        for i in range(min(max(count, 0), 1000)):
            peer = index(self.friends_api, i, 4)
            result.append((peer, (name(self.friends_api, peer) or b"Friend").decode("utf-8", "replace"),
                           state(self.friends_api, peer) != 0))
        return sorted(result, key=lambda friend: (not friend[2], friend[1].casefold()))

    def invite(self, lobby, peer):
        return self.call("InviteUserToLobby", C.c_bool, (U64, U64), lobby, peer)

    def leave(self, lobby):
        self.call("LeaveLobby", None, (U64,), lobby)

    def close(self):
        if self.initialized:
            self._shutdown()
            self.initialized = False
        self.client.restore_environment()
