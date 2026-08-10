"""The sample store: one sqlite file, one row per poll, the fourteen counters and the key
count under a timestamp. It is the only history in the system.

Rates are derived at read time, never stored. Counters only rise, so a drop means the
store was flushed and the delta clamps to zero rather than spiking negative."""

from __future__ import annotations

import sqlite3
import time
from pathlib import Path
from typing import Any

import constants

COUNTERS: dict[str, str] = {
    "records": "records",
    "moc_cnt": "moc-cnt",
    "mtc_cnt": "mtc-cnt",
    "sms_mo_cnt": "sms-mo-cnt",
    "sms_mt_cnt": "sms-mt-cnt",
    "data_cnt": "data-cnt",
    "noans_cnt": "noans-cnt",
    "busy_cnt": "busy-cnt",
    "failed_cnt": "failed-cnt",
    "moc_dur": "moc-dur",
    "mtc_dur": "mtc-dur",
    "data_dur": "data-dur",
    "data_rx": "data-rx",
    "data_tx": "data-tx",
}

COLUMNS: list[str] = ["keys", *COUNTERS]

SUMS: dict[str, tuple[str, ...]] = {
    "calls": ("moc_cnt", "mtc_cnt"),
    "messages": ("sms_mo_cnt", "sms_mt_cnt"),
    "data-vol": ("data_rx", "data_tx"),
    "failures": ("noans_cnt", "busy_cnt", "failed_cnt"),
}

RATIOS: dict[str, tuple[tuple[str, ...], tuple[str, ...]]] = {
    "fail-share": (("noans_cnt", "busy_cnt", "failed_cnt"), ("records",)),
    "avg-moc-dur": (("moc_dur",), ("moc_cnt",)),
    "avg-mtc-dur": (("mtc_dur",), ("mtc_cnt",)),
    "avg-data-dur": (("data_dur",), ("data_cnt",)),
}

WINDOWS: dict[str, int] = {"15m": 900, "1h": 3600, "6h": 21600, "24h": 86400}

RATE_SUFFIX = ":rate"


def metrics() -> list[str]:
    """Every metric name a series can be asked for, rates included."""
    names = [*COUNTERS.values(), "keys", *SUMS, *RATIOS]
    return [*names, *(f"{name}{RATE_SUFFIX}" for name in names if name not in RATIOS)]


class Samples:
    """The sqlite file the sampler appends to and the series endpoint reads.

    One connection per call: a row every few seconds and a few hundred rows a read is
    nothing worth pooling."""

    def __init__(self, path: Path):
        self.path = path
        self.path.parent.mkdir(parents=True, exist_ok=True)
        self._create()

    def append(self, ts: int, keys: int, totals: dict[str, Any]) -> None:
        """One row. A counter the gateway did not send is a zero, not a gap."""
        values: list[int] = [ts, keys]
        values += [int(totals.get(name, 0) or 0) for name in COUNTERS.values()]

        names = ", ".join(["ts", *COLUMNS])
        marks = ", ".join("?" * len(values))
        with self._connect() as connection:
            connection.execute(
                f"INSERT OR REPLACE INTO samples ({names}) VALUES ({marks})", values)

    def sweep(self, retention_days: int = constants.RETENTION_DAYS) -> int:
        """Drop rows past retention, and say how many went."""
        cutoff = int(time.time()) - retention_days * 86400
        with self._connect() as connection:
            return connection.execute("DELETE FROM samples WHERE ts < ?", (cutoff,)).rowcount

    def stats(self) -> dict[str, Any]:
        """What the system screen reports about the file itself."""
        with self._connect() as connection:
            row = connection.execute("SELECT COUNT(*) AS rows, MIN(ts) AS oldest, "
                                     "MAX(ts) AS newest FROM samples").fetchone()

        return {"path": str(self.path),
                "rows": row["rows"],
                "oldest": row["oldest"],
                "newest": row["newest"],
                "bytes": self.path.stat().st_size if self.path.exists() else 0}

    def latest(self) -> dict[str, Any] | None:
        """The newest row, which carries the age of every number read off it."""
        with self._connect() as connection:
            row = connection.execute(
                "SELECT * FROM samples ORDER BY ts DESC LIMIT 1").fetchone()
        return dict(row) if row else None

    def rows(self, since: int) -> list[sqlite3.Row]:
        """The window, plus the row before it so the first rate has a baseline."""
        with self._connect() as connection:
            before = connection.execute(
                "SELECT * FROM samples WHERE ts < ? ORDER BY ts DESC LIMIT 1",
                (since,)).fetchone()
            inside = connection.execute(
                "SELECT * FROM samples WHERE ts >= ? ORDER BY ts", (since,)).fetchall()

        return [before, *inside] if before else list(inside)

    def _connect(self) -> sqlite3.Connection:
        connection = sqlite3.connect(self.path, timeout=5)
        connection.row_factory = sqlite3.Row
        return connection

    def _create(self) -> None:
        columns = ", ".join(f"{name} INTEGER NOT NULL DEFAULT 0" for name in COLUMNS)
        with self._connect() as connection:
            connection.execute("PRAGMA journal_mode=WAL")
            connection.execute("CREATE TABLE IF NOT EXISTS samples "
                               f"(ts INTEGER PRIMARY KEY, {columns})")


def _sum(row: sqlite3.Row, columns: tuple[str, ...]) -> float:
    return float(sum(row[column] for column in columns))


def _value(row: sqlite3.Row, metric: str) -> float | None:
    """One metric off one row, before any rate is taken."""
    if metric == "keys":
        return float(row["keys"])

    for column, reported in COUNTERS.items():
        if reported == metric:
            return float(row[column])

    if metric in SUMS:
        return _sum(row, SUMS[metric])

    if metric in RATIOS:
        top, bottom = RATIOS[metric]
        divisor = _sum(row, bottom)
        return _sum(row, top) / divisor if divisor else None

    raise KeyError(metric)


def _downsample(points: list[list[Any]]) -> list[list[Any]]:
    """Thin a series to what a chart holds, keeping the last point of each bucket."""
    if len(points) <= constants.MAX_POINTS:
        return points

    step = len(points) / constants.MAX_POINTS
    return [points[min(len(points) - 1, int((index + 1) * step) - 1)]
            for index in range(constants.MAX_POINTS)]


def series(samples: Samples, metric: str, window: str) -> dict[str, Any]:
    """One metric over one window. A `:rate` suffix asks for the per-second change,
    averaged over RATE_LAG seconds: the processor writes its totals in batches, so a
    delta between neighbouring samples reads as a spike between two zeroes."""
    rate = metric.endswith(RATE_SUFFIX)
    name = metric[:-len(RATE_SUFFIX)] if rate else metric

    if rate and name in RATIOS:
        raise ValueError(f"no rate for {name}, it is a ratio already")
    if metric not in metrics():
        raise KeyError(metric)

    seconds = WINDOWS.get(window, constants.RETENTION_DAYS * 86400)
    since = int(time.time()) - seconds
    rows = samples.rows(since - constants.RATE_LAG if rate else since)

    points: list[list[Any]] = []
    baseline = 0

    for index, row in enumerate(rows):
        if row["ts"] < since:
            continue

        if not rate:
            points.append([row["ts"], _value(row, name)])
            continue

        while baseline + 1 < index and rows[baseline + 1]["ts"] <= row["ts"] - constants.RATE_LAG:
            baseline += 1

        before = rows[baseline]
        elapsed = row["ts"] - before["ts"]
        now, then = _value(row, name), _value(before, name)
        if elapsed > 0 and now is not None and then is not None:
            points.append([row["ts"], max(0.0, now - then) / elapsed])

    return {"metric": metric,
            "window": window,
            "seconds": seconds,
            "from": since,
            "to": int(time.time()),
            "points": _downsample(points)}
