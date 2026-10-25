"""Explicit in-game smoke tests. Does not modify saves or launch/terminate the game."""
import argparse
import math
import socket
import sys
import time
import uuid
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT))
from server.ghost_coop import protocol, state_files, transport


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("mode", choices=["peer"])
    parser.add_argument("--game-path", default=state_files.DEFAULT_GAME_PATH)
    parser.add_argument("--server", default="127.0.0.1:47777")
    parser.add_argument("--room")
    parser.add_argument("--seconds", type=float, default=30)
    args = parser.parse_args()
    directory = state_files.default_state_dir(args.game_path)
    session = "smoke-" + uuid.uuid4().hex

    def status():
        return (directory / "native_status.txt").read_text(encoding="utf-8").strip()

    if "game ready" not in status():
        raise RuntimeError("Load a save and wait for game ready: " + status())
    if not args.room:
        parser.error("peer requires --room and a running host bridge")
    initial = protocol.parse_local((directory / "local.tsv").read_text())
    if not initial:
        raise RuntimeError("No local snapshot")
    yaw = math.radians(initial["yaw"])
    origin = [initial["p"][0] + math.cos(yaw)*400,
              initial["p"][1] + math.sin(yaw)*400, initial["p"][2]]
    start = time.monotonic()
    seq = 0
    with socket.socket(socket.AF_INET, socket.SOCK_DGRAM) as sock:
        sock.connect(transport.parse_host_port(args.server))
        while time.monotonic() - start < args.seconds:
            t = time.monotonic() - start
            seq += 1
            offset = 130 * math.sin(t)
            speed = 130 * math.cos(t)
            state = dict(session=session, seq=seq, world=initial["world"],
                         p=[origin[0] - math.sin(yaw)*offset, origin[1] + math.cos(yaw)*offset, origin[2]],
                         yaw=initial["yaw"] + (90 if speed >= 0 else -90),
                         v=[-math.sin(yaw)*speed, math.cos(yaw)*speed, 0])
            state.update({key: initial[key] for key in ('weapon','pitch','flags','ability','shot','action','grip')})
            sock.send(protocol.encode(dict(version=protocol.PROTOCOL, type="state", id=session, seq=seq,
                                       name="LAN_Test", state=state), args.room.encode()))
            if seq % 100 == 0:
                print(status(), flush=True)
            time.sleep(0.05)
    time.sleep(2.5)
    print("Disconnected:", status(), flush=True)


if __name__ == "__main__":
    main()
