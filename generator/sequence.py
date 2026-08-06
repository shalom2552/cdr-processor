"""The record counter, kept in a file so sequence numbers keep climbing across runs."""

from __future__ import annotations

from pathlib import Path
from types import TracebackType

from .console import warn


class Sequence:
    def __init__(self, path: Path) -> None:
        self._path = path
        self._value = self._read()
        self._start = self._value

    def _read(self) -> int:
        try:
            return int(self._path.read_text().strip())
        except FileNotFoundError:
            return 0
        except (OSError, ValueError):
            warn("seq", f"{self._path.name} is unreadable, counting from 0 again")
            return 0

    @property
    def value(self) -> int:
        return self._value

    @property
    def generated(self) -> int:
        return self._value - self._start

    def next(self) -> int:
        self._value += 1
        return self._value

    def save(self) -> None:
        try:
            self._path.write_text(str(self._value))
        except OSError as exc:
            warn("seq", f"cannot save the counter to {self._path}: {exc.strerror}")

    def __enter__(self) -> "Sequence":
        return self

    def __exit__(self, exc_type: type[BaseException] | None, exc: BaseException | None,
                 tb: TracebackType | None) -> None:
        self.save()
