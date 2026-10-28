"""Hidden companion controlled by the in-game UMG menu; no launcher windows."""
from __future__ import annotations

import os
import secrets
import socket
import subprocess
import sys
import threading
import time
import uuid
from pathlib import Path

from .protocol import PROTOCOL, TOKEN, clean_name, encode, remote_text
from .state_files import atomic_write, read_local, state_lock
from .steam_bridge import NETWORK, SteamRoster, room_tag


def parse_command(text):
    fields = text.rstrip("\r\n").split("\t")
    if (len(fields) != 7 or fields[0] != "AHGUI1" or not TOKEN.fullmatch(fields[1]) or
            not fields[2].isdigit() or not 0 < int(fields[2]) < 2**53 or
            fields[3] not in {"steam_host", "steam_join", "lan_host", "lan_join", "disconnect"} or
            any(len(value) > 256 or any(ord(c) < 32 for c in value) for value in fields[3:])):
        raise ValueError("invalid menu command")
    return fields[1], int(fields[2]), fields[3], fields[4], fields[5], fields[6]


def clean_status(value):
    return " ".join(str(value).split())[:1024]


class MenuService:
    def __init__(self, directory, game_path, stop, api_factory=None):
        self.directory, self.game_path, self.stop = Path(directory), game_path, stop
        self.api_factory = api_factory
        self.api, self.child, self.lobby = None, None, 0
        self.roster, self.key = None, b""
        self.network_lock = None
        self.session, self.name = uuid.uuid4().hex, "Player"
        self.seq, self.file_seq = 0, 0
        self.peers = set()
        self.started = time.time()
        self.status = "Ready — create or join a lobby"
        self.notice, self.notice_until, self.lan_address = "", 0.0, ""

    def steam(self):
        if self.api is None:
            if self.api_factory is None:
                from .steam_api import SteamAPI
                self.api_factory = SteamAPI
            self.api = self.api_factory()
        return self.api

    def disconnect(self):
        owned = self.child is not None or self.network_lock is not None
        if self.child is not None:
            self.child.terminate()
            self.child.wait(timeout=5)
            self.child = None
        if self.api is not None:
            for peer in self.peers:
                self.api.close_peer(peer)
            if self.lobby:
                self.api.leave(self.lobby)
        self.lobby, self.roster, self.peers = 0, None, set()
        if owned:
            self.file_seq += 1
            atomic_write(self.directory / "remote.tsv", remote_text(self.session, self.file_seq, []))
            atomic_write(self.directory / "steam_lobby.txt", "")
        if self.network_lock is not None:
            self.network_lock.__exit__(None, None, None)
            self.network_lock = None
        self.status = "Disconnected"

    def connect_steam(self, lobby=0, name="Player", room=""):
        name = clean_name(name or "Player")
        api = self.steam()
        self.disconnect()
        guard = state_lock(self.directory)
        guard.__enter__()
        self.network_lock = guard
        self.name = name
        self.status = "Connecting to Steam..."
        self.publish()
        try:
            if not lobby:
                self.key = (room or secrets.token_hex(32)).encode("utf-8")
                self.lobby = api.create_lobby(self.stop, lobby_type=1)  # Friends-only, supports Overlay invites.
                api.set_data(self.lobby, "ahg_network", NETWORK)
                api.set_data(self.lobby, "ahg_room", room_tag(self.key, self.lobby))
                # Steam membership gates admission. Invited friends receive the session key as members.
                api.set_data(self.lobby, "ahg_key", self.key.decode("utf-8"))
                api.open_lobby(self.lobby)
            else:
                self.lobby = api.join_lobby(lobby, self.stop)
                if api.data(self.lobby, "ahg_network") != NETWORK:
                    raise ValueError("Incompatible Steam lobby")
                self.key = (api.data(self.lobby, "ahg_key") or room).encode("utf-8")
                if not self.key or room_tag(self.key, self.lobby) != api.data(self.lobby, "ahg_room"):
                    raise ValueError("Lobby requires a matching room code")
            self.session, self.seq = uuid.uuid4().hex, 0
            api.set_member_session(self.lobby, self.session)
            self.roster = SteamRoster(api.steam_id, self.lobby, self.key)
            self.started = time.time()
            atomic_write(self.directory / "steam_lobby.txt", str(self.lobby) + "\n")
            self.status = "Steam lobby connected"
        except BaseException:
            self.disconnect()
            raise

    def command(self, verb, name, room, address):
        if verb == "disconnect":
            self.disconnect()
        elif verb == "steam_host":
            self.connect_steam(name=name, room=room)
        elif verb == "steam_join":
            if not address.isdigit() or not 0 < int(address) < 2**64:
                raise ValueError("Enter the Steam lobby ID")
            self.connect_steam(int(address), name, room)
        else:
            clean_name(name)
            if not 4 <= len(room) <= 128:
                raise ValueError("LAN room code must contain 4-128 characters")
            if verb == "lan_join":
                from .transport import parse_host_port
                parse_host_port(address)
            self.disconnect()
            launcher = Path(__file__).resolve().parents[1] / "ah_ghost_relay.py"
            args = [sys.executable, str(launcher), "host" if verb == "lan_host" else "client",
                    "--name", name, "--room", room, "--state-dir", str(self.directory)]
            if verb == "lan_join":
                args += ["--server", address]
            self.child = subprocess.Popen(args, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL,
                                          creationflags=subprocess.CREATE_NO_WINDOW if os.name == "nt" else 0)
            self.lan_address = ""
            if verb == "lan_host":
                try:
                    addresses = [ip for ip in socket.gethostbyname_ex(socket.gethostname())[2]
                                 if not ip.startswith(("127.", "169.254."))]
                    self.lan_address = "LAN " + ", ".join(addresses) + ":47777 | "
                except OSError:
                    pass
            self.status = "LAN host started" if verb == "lan_host" else "Connecting to LAN host"

    def step(self):
        now = time.monotonic()
        if self.api is not None:
            self.api.pump()
            if self.api.invites:
                lobby = self.api.invites[-1]
                self.api.invites.clear()
                if lobby != self.lobby:
                    self.connect_steam(lobby, self.name)
        if self.lobby and self.roster is not None:
            members = self.api.members(self.lobby) - {self.api.steam_id}
            for peer in self.peers - members:
                self.api.close_peer(peer)
            self.peers = members
            sessions = {peer: self.api.member_session(self.lobby, peer) for peer in members}
            self.roster.restrict(sessions)
            self.seq += 1
            state = read_local(self.directory / "local.tsv", self.started)
            payload = encode(dict(version=PROTOCOL, type="steam-state", network=NETWORK,
                                  lobby=str(self.lobby), id=str(self.api.steam_id), bridge=self.session,
                                  seq=self.seq, name=self.name, state=state), self.key)
            pending = 0
            for peer in members:
                self.api.accept(peer)
                pending += self.api.send(peer, payload) != 1
            for peer, data in self.api.receive():
                self.roster.receive(peer, data, sessions, now)
            players = self.roster.players(now)
            self.file_seq += 1
            atomic_write(self.directory / "remote.tsv", remote_text(self.session, self.file_seq, players))
            self.status = f"Steam: {len(members)+1}/8 | peers {len(players)}"
            if pending:
                self.status += f" | connecting {pending}"
            atomic_write(self.directory / "bridge_status.txt", self.status + "\n")
        elif self.child is not None:
            if self.child.poll() is not None:
                self.child = None
                self.status = "LAN bridge stopped — check address, room code and UDP 47777"
            else:
                try:
                    self.status = (self.directory / "bridge_status.txt").read_text(encoding="utf-8")[:1024]
                except OSError:
                    pass
        self.publish()

    def publish(self):
        notice = " | " + self.notice if time.monotonic() < self.notice_until else ""
        prefix = self.lan_address if self.child is not None else ""
        atomic_write(self.directory / "gui_status.tsv", f"AHUI1\t{self.lobby}\nS\t{clean_status(prefix + self.status + notice)}\n")

    def close(self):
        try:
            self.disconnect()
        finally:
            if self.api is not None:
                self.api.close()
                self.api = None


def gui_service(args, stop):
    directory = Path(args.state_dir).resolve()
    # The service lock and network lock have different lifetimes; LAN children own bridge.lock.
    with state_lock(directory, filename="gui_service.lock"):
        service = MenuService(directory, args.game_path, stop)
        last = ("", 0)
        parent_handle = None
        if os.name == "nt" and args.parent_pid:
            import ctypes as C
            kernel = C.WinDLL("kernel32", use_last_error=True)
            kernel.OpenProcess.argtypes = [C.c_uint32, C.c_bool, C.c_uint32]
            kernel.OpenProcess.restype = C.c_void_p
            kernel.WaitForSingleObject.argtypes = [C.c_void_p, C.c_uint32]
            kernel.CloseHandle.argtypes = [C.c_void_p]
            parent_handle = kernel.OpenProcess(0x100000, False, args.parent_pid)
            if not parent_handle:
                return
        try:
            try:
                service.steam()  # Receive accepted Overlay invitations even before joining a room.
            except (OSError, RuntimeError) as error:
                service.status = "Steam unavailable: " + str(error)
            while not stop.is_set():
                if parent_handle and kernel.WaitForSingleObject(parent_handle, 0) != 258:
                    break
                try:
                    text = (directory / "gui_command.tsv").read_bytes()[:4097]
                    if len(text) <= 4096:
                        session, seq, verb, name, room, address = parse_command(text.decode("utf-8"))
                        if session != last[0] or seq > last[1]:
                            last = session, seq
                            service.command(verb, name, room, address)
                except (OSError, UnicodeError, ValueError, RuntimeError) as error:
                    if not isinstance(error, FileNotFoundError):
                        service.status = str(error)
                        service.notice, service.notice_until = str(error), time.monotonic() + 8
                try:
                    service.step()
                except (OSError, ValueError, RuntimeError) as error:
                    service.disconnect()
                    service.status = str(error)
                    service.publish()
                stop.wait(.05)
        finally:
            service.close()
            if parent_handle:
                kernel.CloseHandle(parent_handle)
