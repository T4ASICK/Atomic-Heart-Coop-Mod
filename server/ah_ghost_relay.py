#!/usr/bin/env python3
"""Command-line entry point for the ghost co-op relay and bridge."""
if __package__:
    from .ghost_coop.cli import main
else:
    # Embeddable Python's _pth isolation omits the script directory.
    import sys
    from pathlib import Path

    sys.path.insert(0, str(Path(__file__).resolve().parent))
    from ghost_coop.cli import main


if __name__ == "__main__":
    raise SystemExit(main())
