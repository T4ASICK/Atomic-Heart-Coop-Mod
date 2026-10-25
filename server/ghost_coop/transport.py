"""UDP address parsing and portable oversized-datagram detection."""
import errno
import socket

DEFAULT_PORT = 47777


def parse_host_port(value: str) -> tuple[str, int]:
    host, sep, port = value.rpartition(":")
    host, port = (host, int(port)) if sep else (value, DEFAULT_PORT)
    if not host or not 1 <= port <= 65535:
        raise ValueError("expected HOST:PORT (port 1-65535)")
    return socket.gethostbyname(host), port


def oversized_datagram(error: OSError) -> bool:
    # Windows raises WSAEMSGSIZE instead of returning a truncated datagram.
    return error.errno == errno.EMSGSIZE or getattr(error, "winerror", None) == 10040
