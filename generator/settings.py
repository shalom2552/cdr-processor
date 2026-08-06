"""Generator settings, read from the repo's config.toml. Anything the config gets wrong
is reported as a fixable error rather than a traceback."""

from __future__ import annotations

import tomllib
from dataclasses import dataclass
from pathlib import Path

from .console import fail

PACKAGE_DIR = Path(__file__).resolve().parent
ROOT = PACKAGE_DIR.parent
CONFIG_FILE = ROOT / "config.toml"
SEQ_FILE = PACKAGE_DIR / ".seq"

SUPPORTED_FORMAT = "csv"


@dataclass(frozen=True)
class Settings:
    mode: str               # empty when config.toml does not set one
    fmt: str
    separator: str
    ready_dir: Path
    rabbit_url: str
    rabbit_queue: str
    rotate_seconds: int
    gen_interval: float

    @classmethod
    def load(cls, path: Path = CONFIG_FILE) -> "Settings":
        try:
            with open(path, "rb") as fh:
                cfg = tomllib.load(fh)
        except FileNotFoundError:
            fail(f"no config file at {path}",
                 "run the generator from the repo, config.toml lives next to the Makefile")
        except tomllib.TOMLDecodeError as exc:
            fail(f"{path.name} is not valid toml: {exc}")
        except OSError as exc:
            fail(f"cannot read {path}: {exc.strerror}")

        source = cfg.get("source", {})
        fmt = source.get("format", SUPPORTED_FORMAT)
        if fmt != SUPPORTED_FORMAT:
            fail(f"this generator writes '{SUPPORTED_FORMAT}' records, config asks for '{fmt}'",
                 f'set format = "{SUPPORTED_FORMAT}" under [source] in config.toml')

        separator = source.get("csv", {}).get("separator", "|")
        if len(separator) != 1:
            fail(f"the separator must be one character, config asks for '{separator}'",
                 'set separator = "|" under [source.csv] in config.toml')

        gen = cfg.get("generator", {})
        return cls(
            mode=source.get("mode", ""),
            fmt=fmt,
            separator=separator,
            ready_dir=ROOT / source.get("file", {}).get("ready_dir", "records/ready/"),
            rabbit_url=source.get("rabbit", {}).get("url", "amqp://guest:guest@localhost/"),
            rabbit_queue=source.get("rabbit", {}).get("queue", "cdr"),
            rotate_seconds=gen.get("rotate_seconds", 600),
            gen_interval=gen.get("gen_interval", 0.001),
        )

    def relative(self, path: Path) -> str:
        """Path as written in config.toml, for log lines that would otherwise be absolute."""
        try:
            return str(path.relative_to(ROOT))
        except ValueError:
            return str(path)
