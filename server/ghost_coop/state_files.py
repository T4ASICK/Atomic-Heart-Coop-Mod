"""Bounded native snapshots, atomic publication and one-bridge-per-game locking."""
from __future__ import annotations

import os
import time
from contextlib import contextmanager
from pathlib import Path

from .protocol import MAX_PACKET, TIMEOUT, parse_local

DEFAULT_GAME_PATH = r"C:\Program Files (x86)\Steam\steamapps\common\Atomic Heart"


def default_state_dir(game_path: str) -> Path:
    return Path(game_path) / "AtomicHeart/Binaries/Win64/Mods/AHGhostCoop/state"


def atomic_write(path: Path, text: str) -> bool:
    """Keep the previous complete snapshot if antivirus briefly holds the file."""
    tmp = path.with_name(path.name + ".tmp")
    try:
        tmp.write_text(text, encoding="utf-8", newline="\n")
        os.replace(tmp, path)
        return True
    except PermissionError:
        return False


def read_local(path: Path, started: float) -> dict | None:
    """Read one bounded snapshot; inspect the opened file, not a raced path stat."""
    try:
        with path.open("rb") as source:
            mtime = os.fstat(source.fileno()).st_mtime
            if mtime < started - TIMEOUT or not 0 <= time.time() - mtime < TIMEOUT:
                return None
            data = source.read(MAX_PACKET + 1)
        if len(data) > MAX_PACKET:
            return None
        return parse_local(data.decode("utf-8"))
    except (OSError, UnicodeError):
        return None


@contextmanager
def state_lock(directory: Path, filename="bridge.lock"):
    directory.mkdir(parents=True, exist_ok=True)
    with (directory / filename).open("a+b") as lock:
        lock.seek(0)
        lock.write(b"0")
        lock.flush()
        lock.seek(0)
        try:
            if os.name == "nt":
                import msvcrt
                msvcrt.locking(lock.fileno(), msvcrt.LK_NBLCK, 1)
            else:
                import fcntl
                fcntl.flock(lock, fcntl.LOCK_EX | fcntl.LOCK_NB)
        except OSError as error:
            raise RuntimeError("a bridge is already running for this game") from error
        try:
            yield
        finally:
            if os.name == "nt":
                lock.seek(0)
                msvcrt.locking(lock.fileno(), msvcrt.LK_UNLCK, 1)
