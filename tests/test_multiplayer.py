"""Real eight-client LAN exercise and maximum metadata / snapshot sizing."""
import socket
import tempfile
import threading
import time
import unittest
from pathlib import Path
from types import SimpleNamespace

from server.ghost_coop import protocol, relay, state_files
from server.ghost_coop.bridge import bridge
from test_network import state


class EightPlayerTests(unittest.TestCase):
    def test_maximum_seven_peer_roster_fits_both_encodings(self):
        # Include escaped control-like JSON characters, four-byte UTF-8 and maximal grips.
        sample = state(seq=2**53-1, session="s"*64, world="/Game/" + "я"*125,
                       p=[1e8, -1e8, 1e8], yaw=36000, v=[1e5, -1e5, 1e5],
                       weapon=13, pitch=89, flags=255, ability=8, shot=2**32-1, action=2**32-1,
                       grip=[200, -200, 200, .5, .5, .5, .5, 150, -150, 150, .5, .5, .5, .5, .5, .5, .5, .5])
        players = [dict(id=f"{i}"*64, name="🚀"*24, state=sample) for i in range(7)]
        self.assertTrue(protocol.valid_state(sample))
        text = protocol.remote_text("b"*64, 1, players)
        self.assertIn("\t7\n", text)
        self.assertLessEqual(len(text.encode()), protocol.MAX_PACKET)
        message = dict(version=3, type="roster", ack=1, to="b"*64, players=players)
        self.assertEqual(protocol.decode(protocol.encode(message, b"room"), b"room"), message)
        with self.assertRaises(ValueError):
            protocol.remote_text("b", 1, players + [players[0]])
        with self.assertRaises(ValueError):
            protocol.remote_text("b", 1, [players[0], players[0]])

    def test_real_eight_bridges_reject_ninth_expire_and_rejoin(self):
        with tempfile.TemporaryDirectory() as temp, socket.socket(socket.AF_INET, socket.SOCK_DGRAM) as sock:
            sock.bind(("127.0.0.1", 0))
            endpoint = "127.0.0.1:" + str(sock.getsockname()[1])
            server_stop = threading.Event()
            server = threading.Thread(target=relay.serve, args=(sock, b"eight-room", server_stop))
            server.start()
            workers, failures, stops, directories = [], [], [], []

            def start_client(index):
                directory = Path(temp) / str(index)
                directory.mkdir(exist_ok=True)
                stopped = threading.Event()
                args = SimpleNamespace(state_dir=str(directory), game_path="", server=endpoint,
                                       room="eight-room", name="SameName")
                def run():
                    try:
                        bridge(args, stopped)
                    except BaseException as error:
                        failures.append(error)
                worker = threading.Thread(target=run)
                worker.start()
                workers.append(worker)
                stops.append(stopped)
                directories.append(directory)

            sequence = 0
            def wait_counts(indices, expected):
                nonlocal sequence
                deadline = time.monotonic() + 8
                while time.monotonic() < deadline:
                    sequence += 1
                    for index, directory in enumerate(directories):
                        if not stops[index].is_set():
                            state_files.atomic_write(directory / "local.tsv",
                                f"AHG3\tg{index}\t{sequence}\t/Game/Test\t10\t20\t90\t0\t0\t0\t0" + "\t0"*25 + "\n")
                    counts = []
                    for index in indices:
                        try:
                            counts.append(int((directories[index] / "remote.tsv").read_text().splitlines()[0].split("\t")[3]))
                        except (OSError, IndexError, ValueError):
                            counts.append(-1)
                    if counts == [expected]*len(indices):
                        return
                    self.assertFalse(failures, failures)
                    time.sleep(.05)
                self.fail(f"expected {expected} peers for {indices}; got {counts}")

            try:
                for index in range(8):
                    start_client(index)
                wait_counts(list(range(8)), 7)
                start_client(8)
                wait_counts(list(range(8)), 7)
                deadline = time.monotonic() + 3
                while time.monotonic() < deadline:
                    status = (directories[8] / "bridge_status.txt")
                    if status.exists() and "room is full (8 players)" in status.read_text():
                        break
                    time.sleep(.05)
                self.assertIn("room is full (8 players)", status.read_text())
                stops[8].set()
                workers[8].join(2)
                stops[7].set()
                workers[7].join(2)
                wait_counts(list(range(7)), 6)
                start_client(9)
                wait_counts(list(range(7)) + [9], 7)
            finally:
                for stopped in stops:
                    stopped.set()
                for worker in workers:
                    worker.join(3)
                server_stop.set()
                server.join(3)
            self.assertFalse(failures, failures)
            self.assertFalse(any(worker.is_alive() for worker in workers))
