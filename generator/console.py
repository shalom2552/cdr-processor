"""Terminal output for the generator: timestamped status lines on stderr, so records
written to stdout in print mode stay clean."""

from __future__ import annotations

import sys
from datetime import datetime
from typing import NoReturn

if sys.stderr.isatty():
    DIM, CYAN, GREEN, YELLOW, RED = "\033[2m", "\033[36m", "\033[32m", "\033[33m", "\033[31m"
    RESET = "\033[0m"
else:
    DIM = CYAN = GREEN = YELLOW = RED = RESET = ""

_TAG_WIDTH = 7
_INDENT = " " * (len("HH:MM:SS") + 1 + _TAG_WIDTH + 1)


def log(color: str, tag: str, msg: str) -> None:
    print(f"\r{DIM}{datetime.now():%H:%M:%S}{RESET} {color}{tag:<{_TAG_WIDTH}}{RESET} {msg}",
          file=sys.stderr)


def status(tag: str, msg: str) -> None:
    log(CYAN, tag, msg)


def ok(tag: str, msg: str) -> None:
    log(GREEN, tag, msg)


def warn(tag: str, msg: str) -> None:
    log(YELLOW, tag, msg)


def fail(msg: str, *hints: str) -> NoReturn:
    """Report what went wrong plus the commands that fix it, then exit."""
    log(RED, "error", msg)
    for hint in hints:
        print(f"{_INDENT}{DIM}try:{RESET} {hint}", file=sys.stderr)
    raise SystemExit(1)
