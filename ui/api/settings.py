"""The [ui] section of config.toml, read once at startup, with the gateway port from
[query] and the store address from [redis]."""

from __future__ import annotations

import os
import tomllib
from dataclasses import dataclass
from pathlib import Path

import constants

CONFIG_ENV = "CDR_CONFIG"
CONFIG_NAME = "config.toml"


def config_path() -> Path:
    """The file named in the environment, else the nearest one walking up from cwd."""
    named = os.environ.get(CONFIG_ENV)
    if named:
        return Path(named)

    here = Path.cwd().resolve()
    for directory in [here, *here.parents]:
        candidate = directory / CONFIG_NAME
        if candidate.is_file():
            return candidate
    return here / CONFIG_NAME


@dataclass(frozen=True)
class Settings:
    path: Path
    gateway_host: str
    gateway_port: int
    api_port: int
    sample_interval: int
    db_path: Path
    store_host: str
    store_port: int

    @property
    def gateway_url(self) -> str:
        return f"http://{self.gateway_host}:{self.gateway_port}"


def load(path: Path | None = None) -> Settings:
    """Parse config.toml. A missing key takes the default, a missing file takes them all."""
    path = path or config_path()
    try:
        with path.open("rb") as handle:
            doc = tomllib.load(handle)
    except FileNotFoundError:
        doc = {}

    ui = doc.get("ui", {})
    query = doc.get("query", {})
    redis = doc.get("redis", {})

    return Settings(
        path=path,
        gateway_host=ui.get("gateway_host", "127.0.0.1"),
        gateway_port=int(query.get("port", 8080)),
        api_port=int(ui.get("api_port", 8000)),
        sample_interval=int(ui.get("sample_interval", 5)),
        db_path=path.parent / constants.DB_PATH,
        store_host=redis.get("host", "127.0.0.1"),
        store_port=int(redis.get("port", 6379)),
    )
