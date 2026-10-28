"""Find the running Steam client without trusting game-inherited Steam paths."""
from __future__ import annotations

import ctypes as C
import os
from pathlib import Path


def running_steam_directory():
    if os.name != "nt":
        raise RuntimeError("Steam bridge requires Windows")
    kernel = C.WinDLL("kernel32", use_last_error=True)
    kernel.K32EnumProcesses.argtypes = [C.c_void_p, C.c_uint32, C.c_void_p]
    kernel.OpenProcess.argtypes = [C.c_uint32, C.c_bool, C.c_uint32]
    kernel.OpenProcess.restype = C.c_void_p
    kernel.QueryFullProcessImageNameW.argtypes = [C.c_void_p, C.c_uint32, C.c_void_p, C.c_void_p]
    kernel.CloseHandle.argtypes = [C.c_void_p]
    capacity = 1024
    while True:
        processes = (C.c_uint32 * capacity)()
        used = C.c_uint32()
        if not kernel.K32EnumProcesses(processes, C.sizeof(processes), C.byref(used)):
            raise C.WinError(C.get_last_error())
        if used.value < C.sizeof(processes):
            break
        capacity *= 2
    for pid in processes[:used.value // C.sizeof(C.c_uint32)]:
        handle = kernel.OpenProcess(0x1000, False, pid)
        if not handle:
            continue
        try:
            buffer, length = C.create_unicode_buffer(32768), C.c_uint32(32768)
            if kernel.QueryFullProcessImageNameW(handle, 0, buffer, C.byref(length)):
                path = Path(buffer.value)
                if path.name.lower() == "steam.exe" and (path.parent / "steamclient64.dll").is_file():
                    return path.parent
        finally:
            kernel.CloseHandle(handle)
    raise RuntimeError("Start the real Steam client and sign in before opening the lobby")


class SteamClient:
    """Keep the real client loaded for the whole SDK lifetime; leave registry alone."""
    def __init__(self):
        self.routing = None
        self.directory = running_steam_directory()
        self.environment = {key: os.environ.get(key) for key in (
            "SteamAppId", "SteamGameId", "SteamPath", "SteamAppUser", "SteamUser",
            "SteamClientLaunch", "SteamEnv", "SteamOverlayGameId")}
        try:
            for key in self.environment:
                os.environ.pop(key, None)
            os.environ.update(SteamAppId="480", SteamGameId="480", SteamPath=str(self.directory))
            self.dll = C.CDLL(str(self.directory / "steamclient64.dll"))
        except BaseException:
            self.restore_environment()
            raise

    def route_sdk(self, sdk):
        root = Path(__file__).resolve().parents[2]
        candidates = (root / "dist/AHGhostCoopNative.dll",
                      root.parent.parent / "AHGhostCoopNative/dlls/main.dll")
        helper = next((path for path in candidates if path.is_file()), None)
        if helper is None:
            raise RuntimeError("Install the complete updated mod package for real Steam client routing")
        self.helper = C.CDLL(str(helper))
        try:
            create = self.helper.ahg_create_steam_client_route
            self.destroy_route = self.helper.ahg_destroy_steam_client_route
        except AttributeError as error:
            raise RuntimeError("Native DLL is outdated; reinstall the complete mod package") from error
        create.argtypes = [C.c_void_p, C.c_void_p, C.c_wchar_p, C.c_void_p, C.c_uint32]
        create.restype = C.c_void_p
        self.destroy_route.argtypes = [C.c_void_p]
        self.destroy_route.restype = None
        error = C.create_string_buffer(1024)
        self.routing = create(sdk._handle, self.dll._handle,
                              str(self.directory / "steamclient64.dll"), error, len(error))
        if not self.routing:
            raise RuntimeError("Real Steam client routing failed: " + error.value.decode("utf-8", "replace"))

    def restore_environment(self):
        if self.routing:
            self.destroy_route(self.routing)
            self.routing = None
        for key, value in self.environment.items():
            if value is None:
                os.environ.pop(key, None)
            else:
                os.environ[key] = value
