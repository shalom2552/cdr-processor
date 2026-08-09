"""Synthetic CDR generator for the processor: builds csv format records and sends them to
files, stdout, or RabbitMQ, driven by the same config.toml the C++ side reads."""

import sys
from pathlib import Path

THIRD_PARTY = Path(__file__).resolve().parent.parent / "third_party"

if str(THIRD_PARTY) not in sys.path:            # vendored pika
    sys.path.insert(0, str(THIRD_PARTY))
