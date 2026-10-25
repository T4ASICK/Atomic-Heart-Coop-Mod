"""AHG3 validation, authenticated packets and native TSV snapshots."""
from __future__ import annotations

import hashlib
import hmac
import json
import math
import re

PROTOCOL = 3
TIMEOUT = 2.0
MAX_PACKET = 8192
MAX_PLAYERS = 2
TOKEN = re.compile(r"^[A-Za-z0-9_-]{1,64}$")


def clean_name(value: str) -> str:
    value = value.strip()
    if not value or len(value) > 24 or any(ord(c) < 32 or ord(c) == 127 for c in value):
        raise ValueError("name must contain 1-24 printable characters")
    # JSON can contain escaped lone surrogates, which cannot be written as UTF-8.
    try:
        value.encode("utf-8")
    except UnicodeError as error:
        raise ValueError("name must be valid UTF-8") from error
    return value


def valid_grip(value: object) -> bool:
    if value is None:
        return True
    if not isinstance(value, list) or len(value) != 18:
        return False
    for index, number in enumerate(value):
        bound = 200 if index < 3 else 150 if 7 <= index < 10 else 1.001
        if type(number) not in (float, int) or abs(number) > bound or not math.isfinite(number):
            return False
    return all(0.98 <= sum(number * number for number in value[start:start+4]) <= 1.02
               for start in (3, 10, 14))


def valid_state(value: object) -> bool:
    if not isinstance(value, dict) or set(value) != {
            "session", "seq", "world", "p", "yaw", "v",
            "weapon", "pitch", "flags", "ability", "shot", "action", "grip"}:
        return False
    if not isinstance(value["session"], str) or not TOKEN.fullmatch(value["session"]):
        return False
    if type(value["seq"]) is not int or not 0 < value["seq"] < 2**53:
        return False
    world = value["world"]
    if not isinstance(world, str) or not world.startswith("/Game/"):
        return False
    try:
        if len(world.encode("utf-8")) > 256:
            return False
    except UnicodeError:
        return False
    if any(ord(c) < 32 or ord(c) == 127 for c in world):
        return False
    for key, bound in (("p", 1e8), ("v", 1e5)):
        items = value[key]
        if not isinstance(items, list) or len(items) != 3:
            return False
        if any(type(n) not in (float, int) or abs(n) > bound or not math.isfinite(n) for n in items):
            return False
    for key, maximum in (("weapon", 13), ("flags", 255), ("ability", 8),
                         ("shot", 2**32 - 1), ("action", 2**32 - 1)):
        if type(value[key]) is not int or not 0 <= value[key] <= maximum:
            return False
    return valid_grip(value["grip"]) and all(type(value[key]) in (float, int) and abs(value[key]) <= bound and
               math.isfinite(value[key]) for key, bound in (("yaw", 36000), ("pitch", 89)))


def parse_local(text: str) -> dict | None:
    if len(text) > MAX_PACKET:
        return None
    fields = text.strip().split("\t")
    if len(fields) != 36 or fields[0] != "AHG3" or fields[17] not in ("0", "1"):
        return None
    try:
        state = dict(session=fields[1], seq=int(fields[2]), world=fields[3],
                     p=list(map(float, fields[4:7])), yaw=float(fields[7]),
                     v=list(map(float, fields[8:11])), weapon=int(fields[11]),
                     pitch=float(fields[12]), flags=int(fields[13]), ability=int(fields[14]),
                     shot=int(fields[15]), action=int(fields[16]))
        grip = list(map(float, fields[18:36]))
        if fields[17] == "0" and any(number != 0 for number in grip):
            return None
        state["grip"] = grip if fields[17] == "1" else None
    except ValueError:
        return None
    return state if valid_state(state) else None


def invalid_json_constant(value: str):
    raise ValueError("nonfinite JSON number: " + value)


def encode(message: dict, key: bytes) -> bytes:
    body = json.dumps(message, ensure_ascii=False, separators=(",", ":"), allow_nan=False).encode("utf-8")
    return hmac.new(key, body, hashlib.sha256).hexdigest().encode("ascii") + b"\n" + body


def decode(packet: bytes, key: bytes, *, accept_other_versions: bool = False) -> dict | None:
    if len(packet) > MAX_PACKET:
        return None
    digest, sep, body = packet.partition(b"\n")
    if not sep or not hmac.compare_digest(digest, hmac.new(key, body, hashlib.sha256).hexdigest().encode()):
        return None
    try:
        value = json.loads(body, parse_constant=invalid_json_constant)
    except (ValueError, UnicodeError, RecursionError):
        return None
    if not isinstance(value, dict) or type(value.get("version")) is not int:
        return None
    return value if accept_other_versions or value["version"] == PROTOCOL else None


def remote_text(session: str, seq: int, players: list[dict]) -> str:
    rows = []
    for player in players:
        if not isinstance(player, dict) or set(player) != {"id", "name", "state"}:
            raise ValueError("invalid player metadata")
        state = player.get("state")
        pid = player.get("id", "")
        if not isinstance(pid, str) or not TOKEN.fullmatch(pid):
            raise ValueError("invalid player identity")
        name = clean_name(player["name"])
        if state is None:
            continue
        if not valid_state(state):
            raise ValueError("invalid player state")
        fields = [pid, name, state["session"], str(state["seq"]), state["world"],
                  *map(str, state["p"]), str(state["yaw"]), *map(str, state["v"]),
                  *(str(state[field]) for field in ("weapon", "pitch", "flags", "ability", "shot", "action"))]
        fields.extend(["1" if state["grip"] is not None else "0",
                       *map(str, state["grip"] if state["grip"] is not None else [0] * 18)])
        rows.append("\t".join(fields))
    return f"AHGR3\t{session}\t{seq}\t{len(rows)}\n" + "".join(row + "\n" for row in rows)
