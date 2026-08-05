"""Synthetic CDR records and the pipe wire format `PipeParser` reads. A different format
means another formatter here and another parser on the C++ side."""

from __future__ import annotations

import random
from dataclasses import dataclass
from datetime import datetime, timedelta

DIGITS = "0123456789"
USAGE_TYPES = ("MOC", "MTC", "SMS-MO", "SMS-MT", "D", "U", "B", "X")
TIMED_USAGE = ("MOC", "MTC", "D")       # only answered calls and data sessions last a while
DATA_USAGE = "D"

MAX_AGE_SECONDS = 30 * 24 * 60 * 60     # records are dated within the last month
MAX_DURATION = 60 * 60                  # an hour, in seconds
MAX_BYTES = 10_000_000


@dataclass(frozen=True)
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


def _digits(n: int) -> str:
    """n digits, may lead with a zero."""
    return "".join(random.choices(DIGITS, k=n))


def _number(n: int) -> str:
    """n digits with no leading zero, for fields read back as integers."""
    return random.choice(DIGITS[1:]) + _digits(n - 1)


def _imei() -> str:
    return f"{_digits(2)}-{_digits(6)}-{_digits(6)}-{_digits(1)}"


def random_cdr(seq: int) -> Cdr:
    usage = random.choice(USAGE_TYPES)
    is_data = usage == DATA_USAGE
    return Cdr(
        seq=seq,
        imsi=_number(15),
        imei=_imei(),
        usage=usage,
        msisdn=_number(random.randint(11, 15)),
        when=datetime.now() - timedelta(seconds=random.randint(0, MAX_AGE_SECONDS)),
        duration=random.randint(1, MAX_DURATION) if usage in TIMED_USAGE else 0,
        bytes_rx=random.randint(0, MAX_BYTES) if is_data else None,
        bytes_tx=random.randint(0, MAX_BYTES) if is_data else None,
        second_party_imsi="" if is_data else _number(15),
        second_party_msisdn="" if is_data else _number(random.randint(11, 15)),
    )


def pipe_record(cdr: Cdr) -> str:
    """One record as 12 pipe separated fields, empty where a field does not apply."""
    optional = lambda v: "" if v is None else str(v)
    return "|".join([
        str(cdr.seq),
        cdr.imsi,
        cdr.imei,
        cdr.usage,
        cdr.msisdn,
        f"{cdr.when:%d/%m/%Y}",
        f"{cdr.when:%H:%M:%S}",
        str(cdr.duration),
        optional(cdr.bytes_rx),
        optional(cdr.bytes_tx),
        cdr.second_party_imsi,
        cdr.second_party_msisdn,
    ])


def pipe_header(fmt: str, count: int) -> str:
    """The one line header every .cdr file opens with, checked by `FileSource`."""
    return f"CDR|{fmt}|{count}"
