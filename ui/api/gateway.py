"""HTTP to the C++ gateway, the backend's only way out. No redis connection is opened
in python. A gateway that does not answer is 502, a call past its timeout is 504, and
every status the gateway sends passes through."""

from __future__ import annotations

import time
from dataclasses import dataclass, field
from typing import Any

import httpx

import constants


@dataclass
class Call:
    status: int
    body: Any


@dataclass
class Seen:
    status: int = 0
    at: float = 0.0
    error: str = ""


@dataclass
class Gateway:
    """Calls the gateway and remembers how each route last answered."""

    url: str
    timeout: float = constants.REQUEST_TIMEOUT
    routes: dict[str, Seen] = field(default_factory=dict)
    _client: httpx.AsyncClient | None = None

    async def open(self) -> None:
        self._client = httpx.AsyncClient(base_url=self.url, timeout=self.timeout)

    async def close(self) -> None:
        if self._client is not None:
            await self._client.aclose()
            self._client = None

    async def get(self, route: str, params: dict[str, Any] | None = None,
                  timeout: float | None = None) -> Call:
        """One GET. Never raises: a failure is a status and a body the ui can show."""
        if self._client is None:
            return Call(502, {"error": "gateway client not open"})

        try:
            response = await self._client.get(route, params=params or {},
                                              timeout=timeout or self.timeout)
        except httpx.TimeoutException:
            self._seen(route, 504, "timed out")
            return Call(504, {"error": "gateway timed out", "route": route})
        except httpx.HTTPError as error:
            self._seen(route, 502, str(error))
            return Call(502, {"error": "gateway unreachable", "route": route,
                              "detail": str(error)})

        self._seen(route, response.status_code)
        try:
            body = response.json()
        except ValueError:
            body = {"error": "gateway sent no json", "body": response.text[:500]}

        return Call(response.status_code, body)

    def last(self) -> dict[str, dict[str, Any]]:
        """Every route the ui has called, with the status it last gave."""
        return {route: {"status": seen.status, "at": seen.at, "error": seen.error}
                for route, seen in sorted(self.routes.items())}

    def _seen(self, route: str, status: int, error: str = "") -> None:
        self.routes[route] = Seen(status=status, at=time.time(), error=error)
