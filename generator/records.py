"""Synthetic CDR records and the csv wire format `CsvParser` reads. A different format
means another formatter here and another parser on the C++ side."""

from __future__ import annotations

import random
from bisect import bisect
from dataclasses import dataclass
from datetime import datetime, timedelta
from math import exp

_random = random.random
_gauss = random.gauss

DIGITS = "0123456789"
USAGE_TYPES = ("MOC", "MTC", "SMS-MO", "SMS-MT", "D", "U", "B", "X")
USAGE_WEIGHTS = (20, 20, 12, 12, 30, 3, 2, 1)   # data and voice carry the traffic, X is rare
DATA_USAGE = "D"
VOICE_USAGE = ("MOC", "MTC")            # only answered calls and data sessions last a while

DAYS_BACK = 28                          # last four whole weeks, so no weekday is over drawn
MAX_DURATION = 60 * 60                  # an hour, in seconds
MAX_BYTES = 10_000_000

# traffic per hour of the day, from the overnight trough to the evening peak
HOUR_WEIGHTS = (30, 20, 15, 12, 15, 30, 70, 140, 220, 280, 300, 300,
                280, 290, 300, 310, 320, 340, 330, 300, 260, 210, 140, 70)
DAY_WEIGHTS = (1.0, 1.0, 1.0, 1.0, 0.95, 0.7, 0.55)     # monday first, quieter at the weekend

CALL_LOG_MEAN = 3.4                     # ~30s median call, a few run for the full hour
CALL_LOG_SPREAD = 1.15
DATA_LOG_MEAN = 5.2                     # ~3min median session
DATA_LOG_SPREAD = 1.2
RATE_LOG_MEAN = 7.3                     # ~1.5 KB/s down, heavy tail for video
RATE_LOG_SPREAD = 1.0
UPLINK_SHARE = 0.15                     # uplink is a small slice of the downlink

ACTIVITY_SKEW = 2.2                     # a few subscribers make most of the records
CIRCLE = 64                             # a subscriber calls the same small circle
STRANGER_CHANCE = 0.2                   # the rest of the time anyone in the pool
HANDSET_SWAP_CHANCE = 0.005             # a subscriber keeps one imei, mostly


@dataclass(frozen=True, slots=True)
class Cdr:
    seq: int
    imsi: str
    imei: str
    usage: str
    msisdn: str
    when: datetime
    duration: int
    bytes_rx: int | None                # data sessions only, None elsewhere
    bytes_tx: int | None
    second_party_imsi: str              # empty on data sessions
    second_party_msisdn: str


def _cumulative(weights: tuple[float, ...]) -> tuple[float, ...]:
    """Weights as running fractions of the total, for `bisect` to pick from."""
    total = float(sum(weights))
    running, out = 0.0, []
    for w in weights:
        running += w
        out.append(running / total)
    return tuple(out)


_USAGE_CUM = _cumulative(USAGE_WEIGHTS)
_HOUR_CUM = _cumulative(HOUR_WEIGHTS)


def _below(n: int) -> int:
    """A number in [0, n). One call into the C random, where randrange takes several."""
    return int(_random() * n)


def _lognormal(mean: float, spread: float, cap: int) -> int:
    """A long tailed positive number: many small values, a few very large ones."""
    value = int(exp(mean + spread * _gauss(0.0, 1.0)))
    return 1 if value < 1 else cap if value > cap else value


def _digits(n: int) -> str:
    """n digits, may lead with a zero."""
    return f"{_below(10 ** n):0{n}d}"


def _number(n: int) -> str:
    """n digits with no leading zero, for fields read back as integers."""
    low = 10 ** (n - 1)
    return str(low + _below(low * 9))


def _imei() -> str:
    return f"{_digits(2)}-{_digits(6)}-{_digits(6)}-{_digits(1)}"


_pool: tuple[tuple[str, str, str], ...] = ()    # the subscribers records are drawn from


def build_pool(size: int) -> None:
    """The subscribers every record picks from, each an (imsi, msisdn, imei) triple. A smaller
    pool means more records per subscriber, so the counters add up and the links form a graph."""
    global _pool
    _pool = tuple((_number(15), _number(11 + _below(5)), _imei()) for _ in range(size))


def _subscriber(size: int) -> int:
    """A pool index, skewed so the busiest subscribers appear far more often than the rest."""
    return int(size * _random() ** ACTIVITY_SKEW)


def _peer(first: int, size: int) -> int:
    """The other party: usually someone from the subscriber's circle, otherwise a stranger."""
    if _random() < STRANGER_CHANCE:
        second = _subscriber(size)
    else:
        second = (first + 1 + _below(CIRCLE)) % size
    return second if second != first else (first + 1) % size    # never the subscriber itself


def _when(now: datetime) -> datetime:
    """A moment in the last month, busy in the evening and quiet overnight and at weekends."""
    while True:
        day = _below(DAYS_BACK)
        if _random() < DAY_WEIGHTS[(now.weekday() - day) % 7]:
            break
    midnight = now.replace(hour=0, minute=0, second=0, microsecond=0)
    hour = bisect(_HOUR_CUM, _random())
    when = midnight - timedelta(days=day) + timedelta(hours=hour, minutes=_below(60),
                                                      seconds=_below(60))
    return when - timedelta(days=1) if when > now else when      # today's later hours not here yet


def random_cdr(seq: int) -> Cdr:
    usage = USAGE_TYPES[bisect(_USAGE_CUM, _random())]
    is_data = usage == DATA_USAGE
    size = len(_pool)
    first = _subscriber(size)
    second = _peer(first, size)
    if is_data:
        duration = _lognormal(DATA_LOG_MEAN, DATA_LOG_SPREAD, MAX_DURATION)
        rx = min(duration * _lognormal(RATE_LOG_MEAN, RATE_LOG_SPREAD, MAX_BYTES), MAX_BYTES)
        tx = min(int(rx * UPLINK_SHARE * (0.5 + _random())), MAX_BYTES)     # uplink follows the downlink
    elif usage in VOICE_USAGE:
        duration = _lognormal(CALL_LOG_MEAN, CALL_LOG_SPREAD, MAX_DURATION)
        rx = tx = None
    else:
        duration, rx, tx = 0, None, None
    return Cdr(
        seq=seq,
        imsi=_pool[first][0],
        imei=_pool[first][2] if _random() >= HANDSET_SWAP_CHANCE else _imei(),
        usage=usage,
        msisdn=_pool[first][1],
        when=_when(datetime.now()),
        duration=duration,
        bytes_rx=rx,
        bytes_tx=tx,
        second_party_imsi="" if is_data else _pool[second][0],
        second_party_msisdn="" if is_data else _pool[second][1],
    )


def csv_record(cdr: Cdr, sep: str) -> str:
    """One record as 12 separated fields, empty where a field does not apply."""
    w = cdr.when                                # strftime costs more than the fields do
    date = f"{w.day:02d}/{w.month:02d}/{w.year}"
    clock = f"{w.hour:02d}:{w.minute:02d}:{w.second:02d}"
    rx = "" if cdr.bytes_rx is None else cdr.bytes_rx
    tx = "" if cdr.bytes_tx is None else cdr.bytes_tx
    return sep.join((str(cdr.seq), cdr.imsi, cdr.imei, cdr.usage, cdr.msisdn, date, clock,
                     str(cdr.duration), str(rx), str(tx),
                     cdr.second_party_imsi, cdr.second_party_msisdn))


def csv_header(fmt: str, count: int) -> str:
    """The one line header every .cdr file opens with, checked by `FileSource`."""
    return f"CDR|{fmt}|{count}"
