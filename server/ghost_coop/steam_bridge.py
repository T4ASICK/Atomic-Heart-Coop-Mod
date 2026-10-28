"""Spacewar lobbies and authenticated full-mesh snapshots for up to eight players."""
from __future__ import annotations

import hashlib
import hmac
import time
import uuid
from pathlib import Path

from .protocol import MAX_PLAYERS, PROTOCOL, TIMEOUT, TOKEN, clean_name, decode, encode, remote_text, valid_state
from .state_files import atomic_write, default_state_dir, read_local, state_lock

NETWORK = "AHGhostCoop-eight-v1"
INTERVAL = 0.05


def room_tag(key: bytes, lobby: int) -> str:
    return hmac.new(key, f"{NETWORK}:{lobby}".encode(), hashlib.sha256).hexdigest()


class SteamRoster:
    """Reject foreign lobbies, spoofed SteamIDs, old bridge sessions and replayed packets."""
    def __init__(self, local_id: int, lobby: int, key: bytes):
        self.local_id, self.lobby, self.key = local_id, lobby, key
        self.peers: dict[int, tuple[str, int, float, dict]] = {}

    def restrict(self, sessions: dict[int, str]):
        self.peers = {peer: value for peer, value in self.peers.items()
                      if peer in sessions and value[0] == sessions[peer]}

    def receive(self, sender: int, payload: bytes, sessions: dict[int, str], now: float) -> bool:
        if sender == self.local_id or sender not in sessions or len(sessions) >= MAX_PLAYERS:
            return False
        message = decode(payload, self.key)
        if (message is None or message.get("type") != "steam-state" or
                message.get("network") != NETWORK or message.get("lobby") != str(self.lobby) or
                message.get("id") != str(sender)):
            return False
        session, seq = message.get("bridge"), message.get("seq")
        if (not isinstance(session, str) or not TOKEN.fullmatch(session) or
                session != sessions[sender] or type(seq) is not int or not 0 < seq < 2**53):
            return False
        previous = self.peers.get(sender)
        if previous and previous[0] == session and seq <= previous[1]:
            return False
        state = message.get("state")
        if state is not None and not valid_state(state):
            return False
        try:
            name = clean_name(message.get("name"))
        except (ValueError, TypeError, AttributeError):
            return False
        player = dict(id=str(sender), name=name, state=state)
        self.peers[sender] = session, seq, now, player
        return True

    def players(self, now: float) -> list[dict]:
        return [record[3] for peer, record in sorted(self.peers.items()) if now - record[2] < TIMEOUT]


def steam_bridge(args, stop, api_factory=None):
    if api_factory is None:
        from .steam_api import SteamAPI
        api_factory = SteamAPI
    directory = Path(args.state_dir) if args.state_dir else default_state_dir(args.game_path)
    name, key = clean_name(args.name), args.room.encode("utf-8")
    session = uuid.uuid4().hex
    with state_lock(directory):
        api, lobby, peers = None, 0, set()
        file_seq = 0
        try:
            atomic_write(directory / "remote.tsv", remote_text(session, file_seq + 1, []))
            file_seq += 1
            atomic_write(directory / "bridge_status.txt", "connecting to Steam Spacewar\n")
            api = api_factory(args.steam_api)
            if args.command == "steam-host":
                lobby = api.create_lobby(stop)
                for field, value in (("ahg_network", NETWORK), ("ahg_room", room_tag(key, lobby))):
                    api.set_data(lobby, field, value)
                api.open_lobby(lobby)
            else:
                lobby = api.join_lobby(args.lobby, stop)
                if api.data(lobby, "ahg_network") != NETWORK:
                    raise RuntimeError("Lobby belongs to another mod or incompatible version")
                if not hmac.compare_digest(api.data(lobby, "ahg_room"), room_tag(key, lobby)):
                    raise ValueError("wrong room code")
            api.set_member_session(lobby, session)
            atomic_write(directory / "steam_lobby.txt", str(lobby) + "\n")
            print(f"[steam] Spacewar 480 | lobby={lobby} | capacity={MAX_PLAYERS} | SteamID={api.steam_id}", flush=True)
            print("[steam] Share the lobby ID and room code; keep this window open.", flush=True)
            roster = SteamRoster(api.steam_id, lobby, key)
            started, seq, last_log = time.time(), 0, ""
            while not stop.is_set():
                api.pump()
                now = time.monotonic()
                if api.data(lobby, "ahg_network") != NETWORK:
                    raise RuntimeError("Steam lobby metadata lost or changed")
                members = api.members(lobby) - {api.steam_id}
                for peer in peers - members:
                    api.close_peer(peer)
                peers = members
                sessions = {peer: api.member_session(lobby, peer) for peer in peers}
                roster.restrict(sessions)
                state = read_local(directory / "local.tsv", started)
                seq += 1
                payload = encode(dict(version=PROTOCOL, type="steam-state", network=NETWORK,
                                      lobby=str(lobby), id=str(api.steam_id), bridge=session,
                                      seq=seq, name=name, state=state), key)
                errors = 0
                for peer in peers:
                    api.accept(peer)
                    if api.send(peer, payload) != 1:
                        errors += 1
                for sender, message in api.receive():
                    roster.receive(sender, message, sessions, now)
                players = roster.players(now)
                file_seq += 1
                written = atomic_write(directory / "remote.tsv", remote_text(session, file_seq, players))
                status = (f"Steam connected; lobby={lobby}; players={len(members)+1}/{MAX_PLAYERS}; "
                          f"peers={len(players)}; game={'ready' if state else 'waiting for live pawn'}")
                if errors:
                    status += f"; Steam send pending/errors={errors}"
                if not written:
                    status += "; remote file is locked"
                atomic_write(directory / "bridge_status.txt", status + "\n")
                if status != last_log:
                    print("[steam] " + status, flush=True)
                    last_log = status
                stop.wait(INTERVAL)
        except (OSError, ValueError, RuntimeError) as error:
            atomic_write(directory / "bridge_status.txt", f"Steam error: {error}\n")
            raise
        finally:
            atomic_write(directory / "remote.tsv", remote_text(session, file_seq + 1, []))
            atomic_write(directory / "steam_lobby.txt", "")
            if api is not None:
                try:
                    for peer in peers:
                        api.close_peer(peer)
                    if lobby:
                        api.leave(lobby)
                finally:
                    api.close()
            if stop.is_set():
                atomic_write(directory / "bridge_status.txt", "Steam bridge stopped\n")
