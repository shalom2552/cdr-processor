"""Polls /query/health and /query/totals on a timer and appends one row per poll.

A failed poll writes no row: a gap in the data is the truth about that minute. Retention
is swept once a day."""

from __future__ import annotations

import asyncio
import time
from dataclasses import dataclass, field
from typing import Any

import constants
from db import Samples
from gateway import Gateway


@dataclass
class Sampler:
    gateway: Gateway
    samples: Samples
    interval: int
    started: float = 0.0
    polls: int = 0
    failures: int = 0
    last_sample: int = 0
    last_error: str = ""
    _task: asyncio.Task[None] | None = field(default=None, repr=False)
    _swept: float = 0.0

    def start(self) -> None:
        self.started = time.time()
        self._task = asyncio.create_task(self._run())

    async def stop(self) -> None:
        if self._task is None:
            return
        self._task.cancel()
        try:
            await self._task
        except asyncio.CancelledError:
            pass
        self._task = None

    @property
    def running(self) -> bool:
        return self._task is not None and not self._task.done()

    def state(self) -> dict[str, Any]:
        return {"running": self.running,
                "interval": self.interval,
                "started": self.started,
                "polls": self.polls,
                "failures": self.failures,
                "last_sample": self.last_sample,
                "last_error": self.last_error,
                "db": self.samples.stats()}

    async def _run(self) -> None:
        while True:
            await self._poll()
            await self._sweep()
            await asyncio.sleep(self.interval)

    async def _poll(self) -> None:
        self.polls += 1
        health, totals = await asyncio.gather(self.gateway.get("/query/health"),
                                              self.gateway.get("/query/totals"))

        if health.status != 200 or totals.status != 200:
            self.failures += 1
            failed = health if health.status != 200 else totals
            self.last_error = str(failed.body)
            return

        if health.body.get("store") != "up":
            self.failures += 1
            self.last_error = "store down"
            return

        ts = int(time.time())
        await asyncio.to_thread(self.samples.append, ts,
                                int(health.body.get("keys", 0)), totals.body)
        self.last_sample = ts
        self.last_error = ""

    async def _sweep(self) -> None:
        now = time.time()
        if now - self._swept < constants.SWEEP_INTERVAL:
            return
        self._swept = now
        await asyncio.to_thread(self.samples.sweep)
