"""Steam wire validation and bridge lifecycle without contacting Steam accounts."""
import ctypes as C
import tempfile
import threading
import time
import unittest
from pathlib import Path
from types import SimpleNamespace

from server.ghost_coop import protocol, state_files
from server.ghost_coop.steam_api import Identity, LobbyCreated, LobbyEntered, Message, SteamAPI
from server.ghost_coop.steam_bridge import NETWORK, SteamRoster, room_tag, steam_bridge
from test_network import state

KEY = b"steam-room"


def snapshot(sender=2, seq=1, bridge="bridge2", lobby=100, **changes):
    message = dict(version=3, type="steam-state", network=NETWORK, lobby=str(lobby),
                   id=str(sender), seq=seq, bridge=bridge, name="Напарник", state=state())
    message.update(changes)
    return protocol.encode(message, KEY)


class SteamWireTests(unittest.TestCase):
    def test_identity_replay_capacity_expiry_and_rejoin(self):
        roster = SteamRoster(1, 100, KEY)
        sessions = {peer: f"bridge{peer}" for peer in range(2, 9)}
        for peer in sessions:
            self.assertTrue(roster.receive(peer, snapshot(peer, bridge=sessions[peer]), sessions, 1))
        self.assertEqual(len(roster.players(1)), 7)
        self.assertFalse(roster.receive(9, snapshot(9), sessions, 1))
        self.assertFalse(roster.receive(1, snapshot(1), sessions, 1))
        self.assertFalse(roster.receive(2, snapshot(3), sessions, 1))
        self.assertFalse(roster.receive(2, snapshot(lobby=101), sessions, 1))
        self.assertFalse(roster.receive(2, snapshot(network="OtherSpacewarMod"), sessions, 1))
        self.assertFalse(roster.receive(2, snapshot(), sessions, 2))
        self.assertTrue(roster.receive(2, snapshot(seq=2), sessions, 2))
        self.assertFalse(roster.receive(2, snapshot(seq=True), sessions, 2))
        self.assertFalse(roster.receive(2, snapshot(seq=3, state={}), sessions, 2))
        self.assertFalse(roster.receive(2, snapshot(seq=3, name="bad\tname"), sessions, 2))
        self.assertEqual(len(roster.players(3.1)), 1)
        self.assertEqual(roster.players(4.1), [])
        sessions[2] = "restarted"
        roster.restrict(sessions)
        self.assertFalse(roster.receive(2, snapshot(seq=999), sessions, 4.2))
        self.assertTrue(roster.receive(2, snapshot(bridge="restarted"), sessions, 4.2))
        del sessions[2]
        roster.restrict(sessions)
        self.assertNotIn(2, roster.peers)

    def test_wrong_room_and_corrupted_packets(self):
        roster = SteamRoster(1, 100, b"wrong-room")
        for data in (snapshot(), b"bad", b"x" * (protocol.MAX_PACKET+1)):
            self.assertFalse(roster.receive(2, data, {2: "bridge2"}, 1))
        self.assertNotEqual(room_tag(KEY, 100), room_tag(KEY, 101))
        self.assertNotEqual(room_tag(KEY, 100), room_tag(b"wrong", 100))

    def test_windows_x64_flat_api_layout(self):
        self.assertEqual(C.sizeof(Identity), 136)
        self.assertEqual(Identity.data.offset, 8)
        if C.sizeof(C.c_void_p) == 8:
            self.assertEqual(C.sizeof(Message), 216)
            self.assertEqual(Message.peer.offset, 16)
            self.assertEqual(Message.channel.offset, 192)
            self.assertEqual(C.sizeof(LobbyCreated), 16)
            self.assertEqual(C.sizeof(LobbyEntered), 24)

    def test_native_receive_releases_valid_and_oversized_messages(self):
        api = SteamAPI.__new__(SteamAPI)
        api.net = None
        messages = [Message(), Message()]
        payload = C.create_string_buffer(b"payload")
        for message in messages:
            message.data, message.channel = C.addressof(payload), 73
        messages[0].size = 7
        messages[1].size = protocol.MAX_PACKET + 1
        pointers = [C.pointer(message) for message in messages]
        released = []
        def receive(net, channel, slots, maximum):
            for index, pointer in enumerate(pointers):
                slots[index] = pointer
            return len(pointers)
        api._receive = receive
        api._get_identity = lambda identity: 2
        api._release = lambda pointer: released.append(C.addressof(pointer.contents))
        self.assertEqual(api.receive(), [(2, b"payload")])
        self.assertEqual(len(released), 2)


class FakeSteam:
    def __init__(self, stop, metadata=None):
        self.steam_id = 1
        self.stop = stop
        self.metadata = metadata if metadata is not None else {}
        self.closed, self.left, self.opened = False, False, False
        self.sessions, self.sent = {2: "bridge2"}, []
        self.frames = 0

    def create_lobby(self, stop): return 100
    def join_lobby(self, lobby, stop): return lobby
    def set_data(self, lobby, key, value): self.metadata[key] = value
    def data(self, lobby, key): return self.metadata.get(key, "")
    def open_lobby(self, lobby): self.opened = True
    def set_member_session(self, lobby, session): self.sessions[1] = session
    def member_session(self, lobby, peer): return self.sessions[peer]
    def members(self, lobby): return {1, 2}
    def accept(self, peer): pass
    def close_peer(self, peer): pass
    def send(self, peer, payload):
        self.sent.append(protocol.decode(payload, KEY))
        return 1
    def receive(self): return [(2, snapshot(seq=self.frames))]
    def pump(self):
        self.frames += 1
        if self.frames >= 3:
            self.stop.set()
    def leave(self, lobby): self.left = True
    def close(self): self.closed = True


class SteamLifecycleTests(unittest.TestCase):
    def test_seven_remote_players_pass_through_actual_bridge_loop(self):
        with tempfile.TemporaryDirectory() as directory:
            stop = threading.Event()
            api = FakeSteam(stop)
            api.sessions = {peer: f"bridge{peer}" for peer in range(2, 9)}
            api.members = lambda lobby: set(range(1, 9))
            api.receive = lambda: [(peer, snapshot(peer, seq=api.frames, bridge=f"bridge{peer}"))
                                   for peer in range(2, 9)]
            observed = []
            pump = api.pump
            def inspect():
                if api.frames:
                    observed.append((Path(directory) / "remote.tsv").read_text())
                pump()
            api.pump = inspect
            args = SimpleNamespace(command="steam-host", steam_api="", state_dir=directory,
                                   game_path="", name="Host", room=KEY.decode())
            steam_bridge(args, stop, api_factory=lambda path: api)
            self.assertTrue(all("\t7\n" in text for text in observed))
            self.assertEqual(len(api.sent), 21)
            self.assertTrue(api.closed and api.left)

    def test_host_writes_roster_and_clears_on_shutdown(self):
        with tempfile.TemporaryDirectory() as directory:
            stop = threading.Event()
            api = FakeSteam(stop)
            args = SimpleNamespace(command="steam-host", steam_api="", state_dir=directory,
                                   game_path="", name="Host", room=KEY.decode())
            observed = []
            # Inspect real snapshots just before each next frame, before final cleanup.
            pump = api.pump
            def inspect():
                if api.frames:
                    observed.append((Path(directory) / "remote.tsv").read_text())
                pump()
            api.pump = inspect
            steam_bridge(args, stop, api_factory=lambda path: api)
            self.assertTrue(api.opened and api.closed and api.left)
            self.assertTrue(any("\t1\n2\t" in text for text in observed))
            self.assertTrue(all(message["lobby"] == "100" and message["id"] == "1" for message in api.sent))
            self.assertEqual((Path(directory) / "remote.tsv").read_text().splitlines()[0].split("\t")[3], "0")
            self.assertEqual((Path(directory) / "steam_lobby.txt").read_text(), "")
            with state_files.state_lock(Path(directory)):
                pass

    def test_join_wrong_room_leaves_and_reports_error(self):
        with tempfile.TemporaryDirectory() as directory:
            stop = threading.Event()
            api = FakeSteam(stop, {"ahg_network": NETWORK, "ahg_room": room_tag(b"another-room", 100)})
            args = SimpleNamespace(command="steam-join", lobby=100, steam_api="", state_dir=directory,
                                   game_path="", name="Joiner", room=KEY.decode())
            with self.assertRaisesRegex(ValueError, "wrong room code"):
                steam_bridge(args, stop, api_factory=lambda path: api)
            self.assertTrue(api.closed and api.left)
            self.assertIn("wrong room code", (Path(directory) / "bridge_status.txt").read_text())

    def test_send_failure_cleans_up_lobby_and_native_file(self):
        with tempfile.TemporaryDirectory() as directory:
            stop = threading.Event()
            api = FakeSteam(stop)
            def broken_send(peer, payload):
                raise OSError("connection lost")
            api.send = broken_send
            args = SimpleNamespace(command="steam-host", steam_api="", state_dir=directory,
                                   game_path="", name="Host", room=KEY.decode())
            with self.assertRaisesRegex(OSError, "connection lost"):
                steam_bridge(args, stop, api_factory=lambda path: api)
            self.assertTrue(api.closed and api.left)
            self.assertEqual((Path(directory) / "remote.tsv").read_text().splitlines()[0].split("\t")[3], "0")
