"""Synthetic CDR records and the pipe wire format `PipeParser` reads. A different format
means another formatter here and another parser on the C++ side."""

from __future__ import annotations

import random
from dataclasses import dataclass
from datetime import datetime, timedelta

_random = random.random

DIGITS = "0123456789"
USAGE_TYPES = ("MOC", "MTC", "SMS-MO", "SMS-MT", "D", "U", "B", "X")
TIMED_USAGE = ("MOC", "MTC", "D")       # only answered calls and data sessions last a while
DATA_USAGE = "D"

MAX_AGE_SECONDS = 30 * 24 * 60 * 60     # records are dated within the last month
MAX_DURATION = 60 * 60                  # an hour, in seconds
MAX_BYTES = 10_000_000


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


def _below(n: int) -> int:
    """A number in [0, n). One call into the C random, where randrange takes several."""
    return int(_random() * n)


def _digits(n: int) -> str:
    """n digits, may lead with a zero."""
    return f"{_below(10 ** n):0{n}d}"


def _number(n: int) -> str:
    """n digits with no leading zero, for fields read back as integers."""
    low = 10 ** (n - 1)
    return str(low + _below(low * 9))


def _imei() -> str:
    return f"{_digits(2)}-{_digits(6)}-{_digits(6)}-{_digits(1)}"


def random_cdr(seq: int) -> Cdr:
    usage = USAGE_TYPES[_below(len(USAGE_TYPES))]
    is_data = usage == DATA_USAGE
    return Cdr(
        seq=seq,
        imsi=_number(15),
        imei=_imei(),
        usage=usage,
        msisdn=_number(11 + _below(5)),
        when=datetime.now() - timedelta(seconds=_below(MAX_AGE_SECONDS)),
        duration=1 + _below(MAX_DURATION) if usage in TIMED_USAGE else 0,
        bytes_rx=_below(MAX_BYTES) if is_data else None,
        bytes_tx=_below(MAX_BYTES) if is_data else None,
        second_party_imsi="" if is_data else _number(15),
        second_party_msisdn="" if is_data else _number(11 + _below(5)),
    )


def pipe_record(cdr: Cdr) -> str:
    """One record as 12 pipe separated fields, empty where a field does not apply."""
    w = cdr.when                                # strftime costs more than the fields do
    date = f"{w.day:02d}/{w.month:02d}/{w.year}"
    clock = f"{w.hour:02d}:{w.minute:02d}:{w.second:02d}"
    rx = "" if cdr.bytes_rx is None else cdr.bytes_rx
    tx = "" if cdr.bytes_tx is None else cdr.bytes_tx
    return (f"{cdr.seq}|{cdr.imsi}|{cdr.imei}|{cdr.usage}|{cdr.msisdn}|{date}|{clock}"
            f"|{cdr.duration}|{rx}|{tx}|{cdr.second_party_imsi}|{cdr.second_party_msisdn}")


def pipe_header(fmt: str, count: int) -> str:
    """The one line header every .cdr file opens with, checked by `FileSource`."""
    return f"CDR|{fmt}|{count}"
