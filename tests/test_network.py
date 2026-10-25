import socket
import subprocess
import sys
import tempfile
import threading
import time
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT))
from server.ghost_coop import protocol, relay, state_files
GRIP = [10.0, 2.0, -3.0, 0.0, 0.0, 0.0, 1.0,
        15.0, -25.0, 0.0, 0.0, 0.0, 0.0, 1.0, 0.0, 0.0, 0.0, 1.0]


def state(seq=1, world="/Game/Maps/Test", session="game1", **combat):
    value = dict(session=session, seq=seq, world=world, p=[10.0, 20.0, 90.0], yaw=359.0,
                 v=[1.0, 0.0, 0.0], weapon=0, pitch=0.0, flags=0, ability=0, shot=0, action=0, grip=None)
    value.update(combat)
    return value


def packet(pid="a", seq=1, name="SameName", value=None):
    return dict(version=3, type="state", id=pid, seq=seq, name=name, state=value)


class ProtocolTests(unittest.TestCase):
    def test_malformed_and_nonfinite(self):
        for field, value in [("p", [float("nan"), 0, 0]), ("v", [0, 0, float("inf")]),
                             ("p", [10**500, 0, 0]), ("yaw", 10**500),
                             ("yaw", True), ("seq", -1), ("world", "/Game/map\nrow"),
                             ("session", "../bad"), ("weapon", -1), ("weapon", 14),
                             ("weapon", True), ("weapon", 1.0), ("pitch", -89.01),
                             ("pitch", 89.01), ("pitch", float("nan")), ("pitch", float("inf")),
                             ("pitch", 10**500), ("pitch", True), ("flags", -1), ("flags", 256),
                             ("flags", True), ("ability", -1), ("ability", 9), ("ability", 1.0),
                             ("shot", -1), ("shot", 2**32), ("shot", True), ("shot", 1.5),
                             ("action", -1), ("action", 2**32), ("action", True),
                             ("extra", 0)]:
            bad = state()
            bad[field] = value
            self.assertFalse(protocol.valid_state(bad), field)
        self.assertIsNone(protocol.parse_local("AHG1\told\t0"))
        # A v2 file must not silently synthesize missing combat state.
        self.assertIsNone(protocol.parse_local("AHG2\ts\t1\t/Game/M\tnan\t0\t0\t0\t0\t0\t0"))
        for field in ("weapon", "pitch", "flags", "ability", "shot", "action", "grip"):
            bad = state()
            del bad[field]
            self.assertFalse(protocol.valid_state(bad), field)
        self.assertTrue(protocol.valid_state(state(weapon=13, pitch=-89, flags=255, ability=8,
                                               shot=2**32-1, action=2**32-1)))
        for bad_grip in (True, {}, "bad", [], GRIP[:-1], GRIP + [0]):
            self.assertFalse(protocol.valid_state(state(grip=bad_grip)))
        for index, number in [(0, 200.01), (2, -200.01), (7, 150.01), (9, -150.01),
                              (3, 1.002), (6, 0), (10, 1), (17, -0.5),
                              (0, float("nan")), (6, float("inf")), (14, True), (0, 10**500)]:
            bad_grip = GRIP.copy()
            bad_grip[index] = number
            self.assertFalse(protocol.valid_state(state(grip=bad_grip)), (index, number))
        self.assertTrue(protocol.valid_state(state(grip=GRIP)))

    def test_authentication_and_tampering(self):
        message = packet(value=state())
        encoded = protocol.encode(message, b"room")
        self.assertEqual(protocol.decode(encoded, b"room"), message)
        self.assertIsNone(protocol.decode(encoded, b"wrong"))
        self.assertIsNone(protocol.decode(encoded[:-1] + b"x", b"room"))
        self.assertIsNone(protocol.decode(b"x" * 9000, b"room"))
        legacy = dict(message, version=2)
        self.assertIsNone(protocol.decode(protocol.encode(legacy, b"room"), b"room"))
        self.assertIn("incompatible protocol", relay.Room().receive(legacy, ("127.0.0.1", 1), 1)["error"])
        self.assertIsNone(protocol.decode(protocol.encode(dict(message, version=3.0), b"room"), b"room"))

    def test_room_identity_order_capacity_and_expiry(self):
        room = relay.Room()
        room.receive(packet("a", 5, value=state(weapon=3, shot=10, action=20)), ("127.0.0.1", 1), 10)
        response = room.receive(packet("b", 1, value=state()), ("127.0.0.1", 2), 10)
        self.assertEqual([p["id"] for p in response["players"]], ["a"])
        self.assertEqual(len(room.peers), 2)  # Identical display names do not overwrite each other.
        self.assertIn("error", room.receive(packet("a", 4, value=state(shot=999, action=999)),
                                           ("127.0.0.1", 1), 10.1))
        self.assertIsNotNone(room.peers["a"].state)
        self.assertEqual((room.peers["a"].state["shot"], room.peers["a"].state["action"]), (10, 20))
        self.assertIn("error", room.receive(packet("a", 6), ("127.0.0.1", 3), 10.1))
        self.assertIn("error", room.receive(packet("c"), ("127.0.0.1", 3), 10.1))
        response = room.receive(packet("c"), ("127.0.0.1", 3), 13)
        self.assertEqual(response["players"], [])

    def test_utf8_name_and_empty_roster(self):
        name = "\u041a\u0430\u0442\u044f"
        text = protocol.remote_text("bridge", 3, [dict(id="peer", name=name, state=state())])
        self.assertIn("peer\t" + name + "\tgame1\t1\t/Game/Maps/Test", text)
        self.assertEqual(protocol.remote_text("bridge", 4, []), "AHGR3\tbridge\t4\t0\n")
        with self.assertRaises(ValueError):
            protocol.clean_name("bad\tname")
        # Largest metadata and combat fields remain inside the shared byte cap.
        maximum = state(seq=2**53-1, session="s"*64, world="/Game/" + "\u044f"*125,
                        p=[1e8, -1e8, 1e8], yaw=36000, v=[1e5, -1e5, 1e5],
                        weapon=13, pitch=89, flags=255, ability=8, shot=2**32-1, action=2**32-1,
                        grip=[200, -200, 200, 0.5, 0.5, 0.5, 0.5, 150, -150, 150,
                              0.5, 0.5, 0.5, 0.5, 0.5, 0.5, 0.5, 0.5])
        self.assertTrue(protocol.valid_state(maximum))
        maximum_name = "\U0001f680"*24
        self.assertLessEqual(len(protocol.encode(packet("i"*64, 2**53-1, maximum_name, maximum), b"room")),
                             protocol.MAX_PACKET)
        snapshot = protocol.remote_text("b"*64, 2**53-1,
                                     [dict(id="i"*64, name=maximum_name, state=maximum)])
        self.assertLessEqual(len(snapshot.encode("utf-8")), protocol.MAX_PACKET)
        self.assertFalse(protocol.valid_state(dict(maximum, world=maximum["world"] + "x")))

    def test_local_roundtrip(self):
        s = protocol.parse_local("AHG3\tgame1\t1\t/Game/Maps/Test\t10\t20\t90\t359\t1\t0\t0\t0\t0\t0\t0\t0\t0" + "\t0"*19 + "\n")
        self.assertEqual(s, state())
        fields = ["AHG3", "game1", "2", "/Game/Maps/Test", "10", "20", "90", "359", "1", "0", "0",
                  "13", "-89", "255", "8", "4294967295", "4294967295", "1", *map(str, GRIP)]
        maximum = protocol.parse_local("\t".join(fields))
        self.assertEqual(maximum, state(seq=2, weapon=13, pitch=-89, flags=255, ability=8,
                                       shot=2**32-1, action=2**32-1, grip=GRIP))
        rows = protocol.remote_text("bridge", 1, [dict(id="peer", name="Peer", state=maximum)]).splitlines()
        self.assertEqual(len(rows[1].split("\t")), 37)
        self.assertEqual(rows[1].split("\t")[12:18], ["13", "-89.0", "255", "8", "4294967295", "4294967295"])
        self.assertEqual(rows[1].split("\t")[18], "1")
        self.assertEqual(list(map(float, rows[1].split("\t")[19:])), GRIP)
        empty_grip = protocol.remote_text("bridge", 2, [dict(id="peer", name="Peer", state=state())]).splitlines()[1].split("\t")
        self.assertEqual(empty_grip[18:], ["0"]*19)
        for column, value in [(11, "14"), (12, "nan"), (13, "256"), (14, "9"),
                              (15, "4294967296"), (16, "-1"), (15, "1.5"), (17, "2"),
                              (17, "True"), (18, "nan"), (18, "201"), (25, "151"),
                              (24, "0"), (28, "True"), (17, "0")]:
            bad = fields.copy()
            bad[column] = value
            self.assertIsNone(protocol.parse_local("\t".join(bad)), (column, value))
        self.assertIsNone(protocol.parse_local("\t".join(fields[:-1])))
        self.assertIsNone(protocol.parse_local("\t".join(fields + ["0"])))
        with self.assertRaises(ValueError):
            protocol.remote_text("bridge", 2, [dict(id="peer", name="Peer", state=state(shot=2**32))])
        with self.assertRaises(ValueError):
            protocol.remote_text("bridge", 2, [dict(id="peer", name="Peer", state=state(), extra=0)])

    def test_single_bridge_lock(self):
        with tempfile.TemporaryDirectory() as temp:
            directory = Path(temp)
            with state_files.state_lock(directory):
                script = (
                    "import sys; from pathlib import Path; "
                    "sys.path.insert(0, sys.argv[1]); "
                    "from server.ghost_coop.state_files import state_lock; "
                    "state_lock(Path(sys.argv[2])).__enter__()"
                )
                result = subprocess.run([sys.executable, "-c", script, str(ROOT), str(directory)],
                                        cwd=ROOT, capture_output=True)
                self.assertNotEqual(result.returncode, 0)
                self.assertIn(b"a bridge is already running", result.stderr)
            with state_files.state_lock(directory):
                pass


class TwoClientTests(unittest.TestCase):
    def test_real_udp_bridges_and_disconnect(self):
        with tempfile.TemporaryDirectory() as temp, socket.socket(socket.AF_INET, socket.SOCK_DGRAM) as sock:
            sock.bind(("127.0.0.1", 0))
            address = "127.0.0.1:" + str(sock.getsockname()[1])
            stop = threading.Event()
            worker = threading.Thread(target=relay.serve, args=(sock, b"private-room", stop))
            worker.start()
            dirs = [Path(temp) / "a", Path(temp) / "b"]
            clients = []
            try:
                for directory in dirs:
                    directory.mkdir()
                    clients.append(subprocess.Popen([
                        sys.executable, str(ROOT / "server/ah_ghost_relay.py"), "client",
                        "--server", address, "--name", "SameName", "--room", "private-room",
                        "--state-dir", str(directory)], stdout=subprocess.DEVNULL, stderr=subprocess.PIPE))

                def publish(directory, seq):
                    state_files.atomic_write(directory / "local.tsv",
                        f"AHG3\t{directory.name}\t{seq}\t/Game/Test\t10\t20\t90\t30\t1\t0\t0\t13\t-89\t255\t8\t4294967295\t7\t1\t" +
                        "\t".join(map(str, GRIP)) + "\n")

                def remote_count(directory):
                    try:
                        return int((directory / "remote.tsv").read_text().splitlines()[0].split("\t")[3])
                    except (OSError, IndexError, ValueError):
                        return -1

                deadline = time.monotonic() + 6
                seq = 0
                while time.monotonic() < deadline:
                    seq += 1
                    for directory in dirs:
                        publish(directory, seq)
                    if all(remote_count(d) == 1 for d in dirs):
                        break
                    time.sleep(0.05)
                self.assertEqual([remote_count(d) for d in dirs], [1, 1])
                for directory in dirs:
                    row = (directory / "remote.tsv").read_text().splitlines()[1].split("\t")
                    self.assertEqual(row[12:18], ["13", "-89.0", "255", "8", "4294967295", "7"])
                    self.assertEqual(row[18], "1")
                    self.assertEqual(list(map(float, row[19:])), GRIP)
                clients[1].terminate()
                clients[1].wait(timeout=3)
                clients[1].stderr.close()
                deadline = time.monotonic() + 5
                while time.monotonic() < deadline and remote_count(dirs[0]) != 0:
                    seq += 1
                    publish(dirs[0], seq)
                    time.sleep(0.05)
                self.assertEqual(remote_count(dirs[0]), 0)
                self.assertIsNone(clients[0].poll())

                # A fresh client session can rejoin, including after its game/session changes.
                clients[1] = subprocess.Popen([
                    sys.executable, str(ROOT / "server/ah_ghost_relay.py"), "client",
                    "--server", address, "--name", "Rejoined", "--room", "private-room",
                    "--state-dir", str(dirs[1])], stdout=subprocess.DEVNULL, stderr=subprocess.PIPE)
                deadline = time.monotonic() + 5
                while time.monotonic() < deadline:
                    seq += 1
                    for directory in dirs:
                        publish(directory, seq)
                    if all(remote_count(d) == 1 for d in dirs):
                        break
                    time.sleep(0.05)
                self.assertEqual([remote_count(d) for d in dirs], [1, 1])
                self.assertIn("Rejoined", (dirs[0] / "remote.tsv").read_text())

                # Stop the game producer while its bridge stays online: no immortal ghost.
                deadline = time.monotonic() + 5
                while time.monotonic() < deadline and remote_count(dirs[0]) != 0:
                    seq += 1
                    publish(dirs[0], seq)
                    time.sleep(0.05)
                self.assertEqual(remote_count(dirs[0]), 0)
            finally:
                for client in clients:
                    if client.poll() is None:
                        client.terminate()
                    client.wait(timeout=3)
                    if client.stderr:
                        client.stderr.close()
                stop.set()
                worker.join(timeout=1)


if __name__ == "__main__":
    unittest.main()
