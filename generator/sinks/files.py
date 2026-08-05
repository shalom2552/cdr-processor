"""Records to rotating .cdr files in the ready dir. Each file is written to a .tmp name and
renamed into place, so the watcher never sees a half written file."""

from __future__ import annotations

import os
import time
from datetime import datetime
from pathlib import Path

from ..console import fail, ok, status
from ..records import pipe_header
from ..settings import Settings
from .base import Sink


class FileSink(Sink):
    def __init__(self, settings: Settings) -> None:
        super().__init__(settings)
        self._records: list[str] = []
        self._opened_at = time.monotonic()

    def open(self) -> None:
        directory = self.settings.ready_dir
        try:
            directory.mkdir(parents=True, exist_ok=True)
        except OSError as exc:
            fail(f"cannot create the ready dir {directory}: {exc.strerror}",
                 "check source.file.ready_dir in config.toml")
        status("start", f"file mode -> {self.settings.relative(directory)}/, "
                        f"rotate every {self.settings.rotate_seconds}s")
        self._opened_at = time.monotonic()

    def emit(self, record: str) -> None:
        self._records.append(record)
        if time.monotonic() - self._opened_at >= self.settings.rotate_seconds:
            self._rotate()

    def close(self) -> None:
        self._rotate()

    def _rotate(self) -> None:
        if self._records:
            self._write(self._records)
            self._records = []
        self._opened_at = time.monotonic()

    def _write(self, records: list[str]) -> None:
        path = self.settings.ready_dir / f"{datetime.now():%Y%m%d_%H%M%S}.cdr"
        tmp = path.with_suffix(".cdr.tmp")
        try:
            with open(tmp, "w") as fh:
                fh.write(pipe_header(self.settings.fmt, len(records)) + "\n")
                fh.write("\n".join(records) + "\n")
            os.replace(tmp, path)
        except OSError as exc:
            fail(f"cannot write {path}: {exc.strerror}",
                 f"check the permissions and free space on {self.settings.ready_dir}")
        ok("saved", f"{self.settings.relative(path)}  {len(records)} records")
