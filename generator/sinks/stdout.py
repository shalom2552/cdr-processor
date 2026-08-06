"""Records to the terminal, for eyeballing the format."""

from __future__ import annotations

from ..console import status
from .base import Sink, StopEmitting


class PrintSink(Sink):
    def open(self) -> None:
        status("start", "print mode")

    def emit(self, record: str) -> None:
        try:
            print(record)
        except BrokenPipeError:                 # piped into head, or the reader quit
            raise StopEmitting from None
