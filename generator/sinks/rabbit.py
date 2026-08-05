"""Records to a RabbitMQ queue. pika is imported here rather than at module load so the
other modes keep working on a machine that never installed it."""

from __future__ import annotations

from types import ModuleType

from ..console import fail, ok, status
from ..diagnose import broker_hints
from .base import Sink


def _load_pika() -> ModuleType:
    try:
        import pika
    except ModuleNotFoundError as exc:
        if exc.name != "pika":                  # pika is there but one of its own deps is not
            raise
        fail("rabbit mode needs the 'pika' package, which is not installed",
             "pip install pika",
             "or on Arch: sudo pacman -S python-pika",
             "or generate to files instead: python3 -m generator -f")
    return pika


class RabbitSink(Sink):
    def open(self) -> None:
        pika = _load_pika()
        self._pika = pika
        url, queue = self.settings.rabbit_url, self.settings.rabbit_queue

        status("start", f"rabbit mode -> {url} queue={queue}")
        try:
            self._conn = pika.BlockingConnection(pika.URLParameters(url))
            self._channel = self._conn.channel()
            self._channel.queue_declare(queue=queue, durable=True)
        except pika.exceptions.ProbableAuthenticationError:
            fail(f"the broker refused the credentials in {url}",
                 "check the user and password in source.rabbit.url in config.toml")
        except pika.exceptions.AMQPConnectionError:
            fail(f"no RabbitMQ broker answering at {url}", *broker_hints(url))
        except (ValueError, IndexError) as exc:  # URLParameters chokes on a malformed url
            fail(f"bad source.rabbit.url in config.toml: {exc}",
                 "expected something like amqp://guest:guest@localhost/")
        ok("ready", "connected, publishing")

    def emit(self, record: str) -> None:
        try:
            self._channel.basic_publish("", self.settings.rabbit_queue, record.encode())
        except self._pika.exceptions.AMQPError as exc:
            fail(f"lost the broker while publishing: {type(exc).__name__}",
                 *broker_hints(self.settings.rabbit_url))

    def close(self) -> None:
        conn = getattr(self, "_conn", None)
        try:
            if conn is not None and conn.is_open:
                conn.close()
        except Exception:                       # already gone, nothing left to release
            pass
