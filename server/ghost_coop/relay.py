"""Two-player room lifecycle and authenticated UDP relay."""
from __future__ import annotations

import socket
import threading
import time
from dataclasses import dataclass

from .protocol import MAX_PACKET, MAX_PLAYERS, PROTOCOL, TIMEOUT, TOKEN, clean_name, decode, encode, valid_state
from .transport import oversized_datagram


@dataclass
class Peer:
    endpoint: tuple[str, int]
    name: str
    seq: int
    seen: float
    state: dict | None = None


class Room:
    def __init__(self):
        self.peers: dict[str, Peer] = {}

    def expire(self, now: float):
        self.peers = {pid: p for pid, p in self.peers.items() if now - p.seen < TIMEOUT}

    def receive(self, message: dict, endpoint: tuple[str, int], now: float) -> dict:
        self.expire(now)
        if type(message.get("version")) is not int or message["version"] != PROTOCOL:
            return {"error": "incompatible protocol (expected 3)"}
        pid, seq = message.get("id"), message.get("seq")
        if not isinstance(pid, str) or not TOKEN.fullmatch(pid) or type(seq) is not int or not 0 < seq < 2**53:
            return {"error": "invalid packet"}
        try:
            name = clean_name(message.get("name", ""))
        except (ValueError, AttributeError, TypeError):
            return {"error": "invalid name"}
        if message.get("type") != "state":
            return {"error": "invalid packet type"}
        state = message.get("state")
        if state is not None and not valid_state(state):
            return {"error": "invalid state"}
        peer = self.peers.get(pid)
        if peer is not None and (peer.endpoint != endpoint or seq <= peer.seq):
            return {"error": "old packet or endpoint changed"}
        if peer is None and len(self.peers) >= MAX_PLAYERS:
            return {"error": "room is full (2 players)"}
        self.peers[pid] = Peer(endpoint, name, seq, now, state)
        return {"players": [{"id": other, "name": p.name, "state": p.state}
                            for other, p in self.peers.items() if other != pid]}


def serve(sock: socket.socket, key: bytes, stop: threading.Event):
    room = Room()
    sock.settimeout(0.2)
    while not stop.is_set():
        try:
            data, endpoint = sock.recvfrom(MAX_PACKET + 1)
        except (socket.timeout, ConnectionResetError):
            continue
        except OSError as error:
            if oversized_datagram(error):
                continue
            raise
        message = decode(data, key, accept_other_versions=True)
        if message is None:
            continue
        # Only echo routing fields that are safe to encode in an error reply.
        pid, seq = message.get("id"), message.get("seq")
        if (not isinstance(pid, str) or not TOKEN.fullmatch(pid) or
                type(seq) is not int or not 0 < seq < 2**53):
            continue
        response = room.receive(message, endpoint, time.monotonic())
        response.update(version=PROTOCOL, type="roster", ack=seq, to=pid)
        try:
            sock.sendto(encode(response, key), endpoint)
        except OSError:
            pass
