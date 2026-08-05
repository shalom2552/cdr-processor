"""What a destination looks like to the driver loop: open it, emit records into it, close
it. Setup failures are reported through `console.fail`, so a sink either works or exits."""

from __future__ import annotations

from abc import ABC, abstractmethod
from types import TracebackType

from ..settings import Settings


class StopEmitting(Exception):
    """Raised by a sink that cannot take more records but has not failed, such as stdout
    going away when the output is piped into `head`."""


class Sink(ABC):
    def __init__(self, settings: Settings) -> None:
        self.settings = settings

    def open(self) -> None:
        """Announce the destination and set up whatever it needs."""

    @abstractmethod
    def emit(self, record: str) -> None:
        """Hand one formatted record to the destination."""

    def close(self) -> None:
        """Flush and release. Runs even when the run is interrupted."""

    def __enter__(self) -> "Sink":
        self.open()
        return self

    def __exit__(self, exc_type: type[BaseException] | None, exc: BaseException | None,
                 tb: TracebackType | None) -> None:
        self.close()
