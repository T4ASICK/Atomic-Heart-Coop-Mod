"""Two-player LAN relay and file bridge. Python 3.10+, standard library only."""
import argparse
import socket
import sys
import threading

from .bridge import bridge
from .relay import serve
from .state_files import DEFAULT_GAME_PATH
from .transport import DEFAULT_PORT


def build_parser():
    parser = argparse.ArgumentParser(description=__doc__)
    sub = parser.add_subparsers(dest="command", required=True)
    for command in ("server", "host", "client"):
        p = sub.add_parser(command)
        p.add_argument("--room", required=True, help="same private room code on both laptops")
        if command != "client":
            p.add_argument("--host", default="0.0.0.0")
            p.add_argument("--port", type=int, default=DEFAULT_PORT)
        if command != "server":
            p.add_argument("--server", default=f"127.0.0.1:{DEFAULT_PORT}")
            p.add_argument("--name", required=True)
            p.add_argument("--game-path", default=DEFAULT_GAME_PATH)
            p.add_argument("--state-dir", default="")
    return parser


def main(argv=None) -> int:
    args = build_parser().parse_args(argv)
    stop = threading.Event()
    relay, thread = None, None
    try:
        if not 4 <= len(args.room) <= 128:
            raise ValueError("room code must contain 4-128 characters")
        if args.command in ("server", "host"):
            if not 1 <= args.port <= 65535:
                raise ValueError("port must be 1-65535")
            relay = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
            relay.bind((args.host, args.port))
            print(f"[relay] listening on {args.host}:{args.port}, capacity 2", flush=True)
            if args.command == "server":
                serve(relay, args.room.encode("utf-8"), stop)
                return 0
            args.server = f"127.0.0.1:{args.port}"
            thread = threading.Thread(target=serve, args=(relay, args.room.encode("utf-8"), stop))
            thread.start()
        bridge(args, stop)
        return 0
    except KeyboardInterrupt:
        return 0
    except (OSError, ValueError, RuntimeError) as error:
        print(f"[error] {error}", file=sys.stderr)
        return 1
    finally:
        stop.set()
        if thread:
            thread.join(timeout=1.0)
        if relay:
            relay.close()
