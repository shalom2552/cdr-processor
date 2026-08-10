"""Entry point: `python3 -m generator [-p | -f | -r]`, or `make gen ARGS=-f`."""

from __future__ import annotations

import argparse
import signal
import sys
import time

from . import sinks
from .console import status
from .records import build_pool, csv_record, random_cdr
from .sequence import Sequence
from .settings import SEQ_FILE, Settings
from .sinks.base import Sink, StopEmitting
from .totals import Totals


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(prog="python3 -m generator",
                                     description="generate synthetic CDR records")
    group = parser.add_mutually_exclusive_group()
    group.add_argument("-p", "--print", action="store_true", help="print records to the screen")
    group.add_argument("-f", "--file", action="store_true", help="write records to rotating files")
    group.add_argument("-r", "--rabbit", action="store_true", help="stream records to RabbitMQ")
    return parser.parse_args()


def chosen_mode(args: argparse.Namespace, settings: Settings) -> str:
    """A command line flag wins, else source.mode from config.toml, else print."""
    if args.print:
        return "print"
    if args.file:
        return "file"
    if args.rabbit:
        return "rabbit"
    return settings.mode


def generate(sink: Sink, seq: Sequence, interval: float, sep: str, totals: Totals) -> None:
    """Feed records to the sink until the user stops us or the sink says it is done."""
    emit, nxt, count = sink.emit, seq.next, totals.add
    try:
        if interval <= 0:                       # a sleep call per record halves the rate
            while True:
                cdr = random_cdr(nxt())
                emit(csv_record(cdr, sep))
                count(cdr)                      # after emit, a refused record counts nowhere
        else:
            while True:
                cdr = random_cdr(nxt())
                emit(csv_record(cdr, sep))
                count(cdr)
                time.sleep(interval)
    except KeyboardInterrupt:
        status("stop", "stopping...")
    except StopEmitting:
        pass


def main() -> None:
    args = parse_args()
    settings = Settings.load()
    signal.signal(signal.SIGTERM, lambda *_: sys.exit(0))

    build_pool(settings.subscribers)

    totals = Totals()
    with Sequence(SEQ_FILE) as seq:
        status("config", f"gen_interval={settings.gen_interval}s  "
                         f"subscribers={settings.subscribers:,}  seq={seq.value}")
        started = time.perf_counter()
        try:
            with sinks.build(chosen_mode(args, settings), settings) as sink:
                generate(sink, seq, settings.gen_interval, settings.separator, totals)
        finally:
            elapsed = time.perf_counter() - started
            if seq.generated:                   # a run that never started says nothing
                status("stop", f"generated {seq.generated:,} records "
                               f"in {elapsed:.1f}s "
                               f"(~{seq.generated / elapsed:,.0f} records/s)")
            status("totals", "of this run:" + totals.format())
            status("stop", "finished")


if __name__ == "__main__":
    main()
