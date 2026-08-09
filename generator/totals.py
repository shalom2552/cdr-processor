"""What the generator emitted, counted per usage type. The same fourteen fields in the
same order as the C++ side, rendered the same way, so the two blocks can be diffed."""

from __future__ import annotations

from .records import Cdr

NAME_WIDTH = 12                         # values line up past the longest name

FIELDS = ("records", "moc_cnt", "mtc_cnt", "sms_mo_cnt", "sms_mt_cnt", "data_cnt",
          "noans_cnt", "busy_cnt", "failed_cnt", "moc_dur", "mtc_dur", "data_dur",
          "data_rx", "data_tx")


class Totals:
    """One counter per usage type, added to per record and printed when the run ends."""

    __slots__ = FIELDS

    def __init__(self) -> None:
        for field in FIELDS:
            setattr(self, field, 0)

    def add(self, cdr: Cdr) -> None:
        """Count one record. Bound to a local in the driver loop, it runs per record."""
        self.records += 1
        usage = cdr.usage
        if usage == "MOC":
            self.moc_cnt += 1
            self.moc_dur += cdr.duration
        elif usage == "MTC":
            self.mtc_cnt += 1
            self.mtc_dur += cdr.duration
        elif usage == "SMS-MO":
            self.sms_mo_cnt += 1
        elif usage == "SMS-MT":
            self.sms_mt_cnt += 1
        elif usage == "D":
            self.data_cnt += 1
            self.data_dur += cdr.duration
            self.data_rx += cdr.bytes_rx
            self.data_tx += cdr.bytes_tx
        elif usage == "U":
            self.noans_cnt += 1
        elif usage == "B":
            self.busy_cnt += 1
        elif usage == "X":
            self.failed_cnt += 1

    def format(self) -> str:
        """The fourteen lines, indented under the status line they are printed with."""
        return "".join(f"\n\t{field:<{NAME_WIDTH}}{getattr(self, field)}" for field in FIELDS)
