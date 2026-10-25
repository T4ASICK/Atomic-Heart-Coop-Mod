"""Connect one native file producer to the relay and publish validated rosters."""
from __future__ import annotations

import socket
import threading
import time
import uuid
from pathlib import Path

from .protocol import MAX_PACKET, MAX_PLAYERS, PROTOCOL, TIMEOUT, clean_name, decode, encode, remote_text
from .state_files import atomic_write, default_state_dir, read_local, state_lock
from .transport import oversized_datagram, parse_host_port

INTERVAL = 0.05


def bridge(args, stop: threading.Event):
    directory = Path(args.state_dir) if args.state_dir else default_state_dir(args.game_path)
    endpoint = parse_host_port(args.server)
    key = args.room.encode("utf-8")
    pid = uuid.uuid4().hex
    name = clean_name(args.name)
    with state_lock(directory), socket.socket(socket.AF_INET, socket.SOCK_DGRAM) as sock:
        sock.connect(endpoint)  # OS filters datagrams from other endpoints.
        sock.setblocking(False)
        local_path, remote_path = directory / "local.tsv", directory / "remote.tsv"
        started = time.time()
        seq, ack, file_seq = 0, 0, 0
        last_receive = 0.0
        last_log, relay_error = "", ""
        players: list[dict] = []
        print(f"[bridge] {name} -> {endpoint[0]}:{endpoint[1]} | {directory}", flush=True)
        try:
            while not stop.is_set():
                now = time.monotonic()
                state = read_local(local_path, started)
                seq += 1
                packet = dict(version=PROTOCOL, type="state", id=pid, seq=seq, name=name, state=state)
                try:
                    sock.send(encode(packet, key))
                except (BlockingIOError, ConnectionResetError, ConnectionRefusedError):
                    pass
                for _ in range(32):
                    try:
                        data = sock.recv(MAX_PACKET + 1)
                    except (BlockingIOError, ConnectionResetError, ConnectionRefusedError):
                        break
                    except OSError as error:
                        if oversized_datagram(error):
                            continue
                        raise
                    reply = decode(data, key, accept_other_versions=True)
                    if reply is None or reply.get("type") != "roster" or reply.get("to") != pid:
                        continue
                    if reply["version"] != PROTOCOL:
                        relay_error = f"incompatible relay protocol (expected 3; received {reply['version']})"
                        players = []
                        continue
                    new_ack = reply.get("ack")
                    if type(new_ack) is not int or not ack < new_ack <= seq:
                        continue
                    incoming = reply.get("players", [])
                    if not isinstance(incoming, list) or len(incoming) > MAX_PLAYERS - 1:
                        continue
                    try:
                        remote_text(pid, file_seq + 1, incoming)
                        error = reply.get("error", "")
                        if (not isinstance(error, str) or len(error) > 256 or
                                any(ord(c) < 32 or ord(c) == 127 for c in error)):
                            continue
                        error.encode("utf-8")
                    except (ValueError, KeyError, TypeError, AttributeError):
                        continue
                    ack, last_receive, players = new_ack, now, incoming
                    relay_error = error
                if now - last_receive > TIMEOUT:
                    players = []
                file_seq += 1
                written = atomic_write(remote_path, remote_text(pid, file_seq, players))
                status = relay_error or ("relay connected" if now - last_receive <= TIMEOUT else
                                        "waiting for relay (check IP, room code, UDP 47777 firewall)")
                status += f"; game={'ready' if state else 'waiting for live pawn'}; peers={len(players)}"
                if not written:
                    status += "; remote file is locked"
                atomic_write(directory / "bridge_status.txt", status + "\n")
                if status != last_log:
                    print("[bridge] " + status, flush=True)
                    last_log = status
                stop.wait(INTERVAL)
        finally:
            atomic_write(remote_path, remote_text(pid, file_seq + 1, []))
            atomic_write(directory / "bridge_status.txt", "bridge stopped\n")
