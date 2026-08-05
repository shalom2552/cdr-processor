"""The destinations records can go to, one class per `source.mode`."""

from __future__ import annotations

from ..console import fail
from ..settings import Settings
from .base import Sink, StopEmitting
from .files import FileSink
from .rabbit import RabbitSink
from .stdout import PrintSink

SINKS: dict[str, type[Sink]] = {
    "print": PrintSink,
    "file": FileSink,
    "rabbit": RabbitSink,
}
DEFAULT_MODE = "print"

__all__ = ["SINKS", "DEFAULT_MODE", "Sink", "StopEmitting", "build"]


def build(mode: str, settings: Settings) -> Sink:
    """Sink for a mode name. An unset mode prints, an unknown one is a config error."""
    sink = SINKS.get(mode or DEFAULT_MODE)
    if sink is None:
        fail(f"unknown mode '{mode}'",
             f"set source.mode in config.toml to one of: {', '.join(SINKS)}",
             "or choose on the command line: -p, -f, -r")
    return sink(settings)
