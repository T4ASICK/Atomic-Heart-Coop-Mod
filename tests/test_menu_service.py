"""Menu commands, accepted Steam invites, hidden LAN child and session cleanup."""
import ctypes as C
import tempfile
import threading
import unittest
from pathlib import Path
from unittest import mock

from server.ghost_coop import state_files
from server.ghost_coop.gui_service import MenuService, parse_command
from server.ghost_coop.steam_api import AsyncCall, Callback, LobbyCreated, SteamAPI
from server.ghost_coop.steam_bridge import NETWORK, room_tag
from test_steam import FakeSteam


class ServiceSteam(FakeSteam):
    def __init__(self, stop):
        super().__init__(stop)
        self.invites = []
        self.steam_id = 1
    def create_lobby(self, stop, lobby_type=3):
        self.lobby_type = lobby_type
        return 100
    def pump(self):
        self.frames += 1
    def receive(self): return []


class MenuServiceTests(unittest.TestCase):
    def test_command_is_bounded_and_preserves_empty_optional_fields(self):
        command = parse_command("AHGUI1\tgame\t1\tsteam_host\tPlayer\t\t\n")
        self.assertEqual(command, ("game", 1, "steam_host", "Player", "", ""))
        for text in ("", "AHGUI1\tbad/id\t1\tdisconnect\t\t\t",
                     "AHGUI1\ts\t0\tdisconnect\t\t\t",
                     "AHGUI1\ts\t1\tother\t\t\t", "AHGUI1\ts\t1\tdisconnect\tx\nrow\t\t"):
            with self.assertRaises(ValueError):
                parse_command(text)

    def test_create_friends_lobby_without_room_code_and_leave_releases_network_lock(self):
        with tempfile.TemporaryDirectory() as directory:
            stop = threading.Event()
            api = ServiceSteam(stop)
            service = MenuService(directory, "", stop, api_factory=lambda: api)
            try:
                service.command("steam_host", "Host", "", "")
                self.assertEqual(service.lobby, 100)
                self.assertEqual(api.lobby_type, 1)
                self.assertTrue(api.opened)
                self.assertEqual(api.metadata["ahg_network"], NETWORK)
                self.assertTrue(api.metadata["ahg_key"])
                self.assertEqual(room_tag(api.metadata["ahg_key"].encode(), 100), api.metadata["ahg_room"])
                with self.assertRaises(RuntimeError):
                    with state_files.state_lock(Path(directory)):
                        pass
                service.step()
                self.assertTrue((Path(directory) / "gui_status.tsv").read_text().startswith("AHUI1\t100\n"))
                service.command("disconnect", "", "", "")
                with state_files.state_lock(Path(directory)):
                    pass
                self.assertEqual(service.lobby, 0)
                self.assertTrue(api.left)
            finally:
                service.close()
            self.assertTrue(api.closed)

    def test_accepted_overlay_invitation_joins_lobby_using_member_key(self):
        with tempfile.TemporaryDirectory() as directory:
            stop = threading.Event()
            api = ServiceSteam(stop)
            api.metadata.update(ahg_network=NETWORK, ahg_key="private-key",
                                ahg_room=room_tag(b"private-key", 200))
            api.invites = [200]
            service = MenuService(directory, "", stop, api_factory=lambda: api)
            try:
                service.steam()
                service.step()
                self.assertEqual(service.lobby, 200)
                self.assertEqual(service.key, b"private-key")
                self.assertEqual(api.invites, [])
            finally:
                service.close()

    def test_manual_bridge_cannot_be_overwritten_by_menu_service(self):
        with tempfile.TemporaryDirectory() as directory:
            stop = threading.Event()
            api = ServiceSteam(stop)
            service = MenuService(directory, "", stop, api_factory=lambda: api)
            sentinel = Path(directory) / "remote.tsv"
            sentinel.write_text("existing manual bridge snapshot")
            with state_files.state_lock(Path(directory)):
                with self.assertRaisesRegex(RuntimeError, "already running"):
                    service.connect_steam()
                self.assertEqual(sentinel.read_text(), "existing manual bridge snapshot")
            service.close()

    def test_lan_child_is_hidden_and_terminated_on_disconnect(self):
        with tempfile.TemporaryDirectory() as directory:
            service = MenuService(directory, "", threading.Event())
            child = mock.Mock()
            with mock.patch("server.ghost_coop.gui_service.subprocess.Popen", return_value=child) as launch:
                service.command("lan_join", "Player", "room", "127.0.0.1:47777")
                args, kwargs = launch.call_args
                self.assertIn("--state-dir", args[0])
                self.assertIn("127.0.0.1:47777", args[0])
                self.assertIn("creationflags", kwargs)
                service.disconnect()
                child.terminate.assert_called_once()
                child.wait.assert_called_once()

    def test_failed_join_leaves_lobby_and_releases_lock(self):
        with tempfile.TemporaryDirectory() as directory:
            stop = threading.Event()
            api = ServiceSteam(stop)
            api.metadata["ahg_network"] = "unrelated-spacewar-app"
            service = MenuService(directory, "", stop, api_factory=lambda: api)
            with self.assertRaisesRegex(ValueError, "Incompatible"):
                service.connect_steam(200)
            self.assertTrue(api.left)
            with state_files.state_lock(Path(directory)):
                pass
            service.close()


class SteamDispatchTests(unittest.TestCase):
    def test_manual_dispatch_preserves_call_result_and_accepted_invite(self):
        api = SteamAPI.__new__(SteamAPI)
        api.pipe, api.results, api.invites = 1, {}, []
        value = LobbyCreated(1, 123)
        complete = AsyncCall(7, 513, C.sizeof(value))
        invitation = (C.c_uint64 * 2)(456, 2)
        events = [(703, C.addressof(complete), C.sizeof(complete)),
                  (333, C.addressof(invitation), C.sizeof(invitation))]
        api._frame = lambda pipe: None
        released = []
        api._free = lambda pipe: released.append(pipe)
        def next_callback(pipe, pointer):
            if not events:
                return False
            target = C.cast(pointer, C.POINTER(Callback)).contents
            target.event, target.data, target.size = events.pop(0)
            return True
        def get_result(pipe, handle, buffer, size, event, failed):
            C.memmove(buffer, C.byref(value), C.sizeof(value))
            return True
        api._next, api._call_result = next_callback, get_result
        api.pump()
        self.assertEqual(api.invites, [456])
        self.assertEqual(len(released), 2)
        result = api.wait(7, LobbyCreated, 513, threading.Event())
        self.assertEqual((result.result, result.lobby), (1, 123))
        self.assertEqual(api.results, {})
