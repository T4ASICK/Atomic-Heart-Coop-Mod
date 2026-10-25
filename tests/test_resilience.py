"""Loopback regression tests; never read or write the installed game."""
import hashlib
import hmac
import json
import socket
import subprocess
import sys
import tempfile
import time
from types import SimpleNamespace
import unittest
from pathlib import Path
from unittest import mock

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT))
from server.ghost_coop import protocol, state_files
from server.ghost_coop import bridge as bridge_module
KEY = b"resilience-room"


def request(seq=1):
    return dict(version=3, type="state", id="probe", seq=seq, name="Probe", state=None)


def signed_json(value):
    # Deliberately permits malformed JSON numbers that production encode rejects.
    body = json.dumps(value, ensure_ascii=True).encode("ascii")
    return hmac.new(KEY, body, hashlib.sha256).hexdigest().encode() + b"\n" + body


class DatagramTests(unittest.TestCase):
    def test_relay_survives_oversized_and_invalid_envelopes(self):
        with socket.socket(socket.AF_INET, socket.SOCK_DGRAM) as probe:
            probe.bind(("127.0.0.1", 0))
            port = probe.getsockname()[1]
        process = subprocess.Popen([
            sys.executable, str(ROOT / "server/ah_ghost_relay.py"), "server",
            "--host", "127.0.0.1", "--port", str(port), "--room", KEY.decode()],
            stdout=subprocess.DEVNULL, stderr=subprocess.PIPE)
        with socket.socket(socket.AF_INET, socket.SOCK_DGRAM) as client:
            client.settimeout(0.1)
            endpoint = ("127.0.0.1", port)

            def expect_reply(seq):
                deadline = time.monotonic() + 3
                while time.monotonic() < deadline:
                    self.assertIsNone(process.poll(), "relay exited instead of dropping invalid traffic")
                    client.sendto(protocol.encode(request(seq), KEY), endpoint)
                    try:
                        reply = protocol.decode(client.recv(8192), KEY)
                    except (socket.timeout, ConnectionResetError):
                        continue
                    if reply and reply.get("ack") == seq:
                        self.assertEqual(reply.get("players"), [])
                        self.assertNotIn("error", reply)
                        return
                self.fail("relay did not answer valid request after invalid traffic")

            try:
                expect_reply(1)
                # Authenticated old clients get an explicit compatibility error,
                # without occupying a slot or advancing the accepted sequence.
                client.sendto(signed_json(dict(request(2), version=2)), endpoint)
                mismatch = protocol.decode(client.recv(8192), KEY)
                self.assertEqual(mismatch["version"], 3)
                self.assertIn("incompatible protocol", mismatch["error"])
                poisons = [b"x" * 60000]
                for seq in (float("nan"), float("inf"), {"bad": "\ud800"}):
                    poisons.append(signed_json(request(seq)))
                bad_id = request()
                bad_id["id"] = "\ud800"
                poisons.append(signed_json(bad_id))
                for seq, poison in enumerate(poisons, 2):
                    client.sendto(poison, endpoint)
                    expect_reply(seq)
            finally:
                process.terminate()
                process.wait(timeout=3)
                process.stderr.close()

    def test_bridge_survives_oversized_relay_datagram(self):
        with tempfile.TemporaryDirectory() as temp, socket.socket(socket.AF_INET, socket.SOCK_DGRAM) as server:
            server.bind(("127.0.0.1", 0))
            server.settimeout(3)
            process = subprocess.Popen([
                sys.executable, str(ROOT / "server/ah_ghost_relay.py"), "client",
                "--server", "127.0.0.1:" + str(server.getsockname()[1]),
                "--room", KEY.decode(), "--name", "Probe", "--state-dir", temp],
                stdout=subprocess.DEVNULL, stderr=subprocess.PIPE)
            try:
                data, endpoint = server.recvfrom(8192)
                self.assertIsNotNone(protocol.decode(data, KEY))
                server.sendto(b"x" * 60000, endpoint)
                message = protocol.decode(data, KEY)
                for error in ("\ud800", "bad\nstatus", {"bad": "error"}):
                    bad_reply = dict(version=3, type="roster", to=message["id"],
                                     ack=message["seq"], players=[], error=error)
                    server.sendto(signed_json(bad_reply), endpoint)
                deadline = time.monotonic() + 3
                connected = False
                while time.monotonic() < deadline:
                    self.assertIsNone(process.poll(), "bridge exited on oversized datagram")
                    try:
                        data, endpoint = server.recvfrom(8192)
                    except socket.timeout:
                        self.assertIsNone(process.poll(), "bridge exited on oversized datagram")
                        continue
                    message = protocol.decode(data, KEY)
                    reply = dict(version=3, type="roster", to=message["id"], ack=message["seq"], players=[])
                    server.sendto(protocol.encode(reply, KEY), endpoint)
                    status = Path(temp) / "bridge_status.txt"
                    try:
                        connected = "relay connected" in status.read_text(encoding="utf-8")
                    except OSError:
                        pass
                    if connected:
                        break
                self.assertTrue(connected, "bridge did not recover communication")
            finally:
                process.terminate()
                process.wait(timeout=3)
                process.stderr.close()


    def test_bridge_loss_reordering_timeout_and_recovery(self):
        with tempfile.TemporaryDirectory() as temp, socket.socket(socket.AF_INET, socket.SOCK_DGRAM) as server:
            server.bind(("127.0.0.1", 0))
            server.settimeout(0.1)
            process = subprocess.Popen([
                sys.executable, str(ROOT / "server/ah_ghost_relay.py"), "client",
                "--server", "127.0.0.1:" + str(server.getsockname()[1]),
                "--room", KEY.decode(), "--name", "Probe", "--state-dir", temp],
                stdout=subprocess.DEVNULL, stderr=subprocess.PIPE)
            remote = Path(temp) / "remote.tsv"

            def roster_is(count, x=None):
                try:
                    rows = remote.read_text(encoding="utf-8").splitlines()
                    return int(rows[0].split("\t")[3]) == count and (
                        x is None or (float(rows[1].split("\t")[5]) == x and
                            rows[1].split("\t")[12:18] == ["3", "12", "5", "2", str(x), str(x+1)] and
                            rows[1].split("\t")[18:] == ["0"]*19))
                except (OSError, IndexError, ValueError):
                    return False

            def exchange(x, old=False, version=3):
                data, endpoint = server.recvfrom(8192)
                message = protocol.decode(data, KEY)
                peer = dict(id="peer", name="Peer", state=dict(session="game", seq=message["seq"],
                    world="/Game/Test", p=[x, 0, 90], yaw=0, v=[0, 0, 0],
                    weapon=3, pitch=12, flags=5, ability=2, shot=x, action=x+1, grip=None))
                reply = dict(version=version, type="roster", to=message["id"],
                             ack=1 if old else message["seq"], players=[peer])
                server.sendto(protocol.encode(reply, KEY), endpoint)

            def until(predicate, action, timeout=4):
                deadline = time.monotonic() + timeout
                while time.monotonic() < deadline:
                    self.assertIsNone(process.poll(), "bridge exited during simulated packet loss")
                    try:
                        action()
                    except socket.timeout:
                        pass
                    if predicate():
                        return
                self.fail("bridge did not reach expected network state")

            try:
                # Drop initial replies; delayed responses must still permit a join.
                for _ in range(5):
                    server.settimeout(3)
                    server.recvfrom(8192)
                server.settimeout(0.1)
                def incompatible_status():
                    try:
                        return "incompatible relay protocol" in (Path(temp) / "bridge_status.txt").read_text()
                    except OSError:
                        return False
                until(incompatible_status, lambda: exchange(10, version=2))
                self.assertTrue(roster_is(0))
                until(lambda: roster_is(1, 10), lambda: exchange(10))
                # Older authenticated replies must not replace the displayed position.
                deadline = time.monotonic() + 0.25
                while time.monotonic() < deadline:
                    exchange(999, old=True)
                self.assertTrue(roster_is(1, 10))
                # Drain requests but drop all replies until timeout clears the visual.
                until(lambda: roster_is(0), lambda: server.recvfrom(8192))
                until(lambda: roster_is(1, 20), lambda: exchange(20))
            finally:
                process.terminate()
                process.wait(timeout=3)
                process.stderr.close()


class LocalSnapshotTests(unittest.TestCase):
    def test_bridge_continues_after_send_buffer_pressure(self):
        with tempfile.TemporaryDirectory() as temp:
            sock = mock.MagicMock()
            sock.__enter__.return_value = sock
            sock.send.side_effect = [BlockingIOError(), 1]
            sock.recv.side_effect = BlockingIOError()
            stop = mock.Mock()
            stop.is_set.side_effect = [False, False, True]
            args = SimpleNamespace(state_dir=temp, server="127.0.0.1:47777",
                                   room=KEY.decode(), name="Probe")
            with mock.patch.object(bridge_module.socket, "socket", return_value=sock), mock.patch("builtins.print"):
                bridge_module.bridge(args, stop)
            self.assertEqual([protocol.decode(call.args[0], KEY)["seq"]
                              for call in sock.send.call_args_list], [1, 2])
            header = (Path(temp) / "remote.tsv").read_text().strip().split("\t")
            self.assertEqual(header[2:], ["3", "0"])

    def test_local_read_is_bounded_even_when_stat_is_small(self):
        source = mock.MagicMock()
        source.__enter__.return_value = source
        source.fileno.return_value = 7
        source.read.return_value = b"x" * (protocol.MAX_PACKET + 1)
        path = mock.Mock()
        path.open.return_value = source
        with mock.patch.object(state_files.os, "fstat", return_value=mock.Mock(st_mtime=time.time())):
            self.assertIsNone(state_files.read_local(path, time.time()))
        source.read.assert_called_once_with(protocol.MAX_PACKET + 1)

    def test_invalid_utf8_stale_future_and_valid_local_files(self):
        with tempfile.TemporaryDirectory() as temp:
            path = Path(temp) / "local.tsv"
            started = time.time()
            path.write_bytes(b"\xff")
            self.assertIsNone(state_files.read_local(path, started))
            path.write_text("AHG3\ts\t1\t/Game/Test\t0\t0\t90\t0\t0\t0\t0\t0\t0\t0\t0\t0\t0" +
                            "\t0"*19 + "\n", encoding="utf-8")
            self.assertEqual(state_files.read_local(path, started)["seq"], 1)
            for mtime in (started - 10, started + 10):
                state_files.os.utime(path, (mtime, mtime))
                self.assertIsNone(state_files.read_local(path, started))


if __name__ == "__main__":
    unittest.main()
