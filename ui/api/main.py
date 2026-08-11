"""The ui backend: one proxy endpoint per gateway route, the sampler's series, and the
config file. It adds no data of its own and writes nothing to the store."""

from __future__ import annotations

from contextlib import asynccontextmanager
from pathlib import Path
from typing import Any

from fastapi import FastAPI, Query
from fastapi.responses import JSONResponse
from fastapi.staticfiles import StaticFiles

import config_file
import constants
import db
import settings as config
from db import Samples
from gateway import Call, Gateway
from sampler import Sampler

cfg = config.load()
gateway = Gateway(cfg.gateway_url)
samples = Samples(cfg.db_path)
sampler = Sampler(gateway, samples, cfg.sample_interval)

WEB_DIST = Path(__file__).resolve().parents[1] / "web" / "dist"


@asynccontextmanager
async def lifespan(_: FastAPI):
    await gateway.open()
    sampler.start()
    yield
    await sampler.stop()
    await gateway.close()


app = FastAPI(title="CDR-Insight", lifespan=lifespan)


def sent(call: Call) -> JSONResponse:
    return JSONResponse(status_code=call.status, content=call.body)


@app.get("/api/health")
async def health() -> JSONResponse:
    return sent(await gateway.get("/query/health"))


@app.get("/api/totals")
async def totals() -> JSONResponse:
    return sent(await gateway.get("/query/totals"))


@app.get("/api/top/{board}")
async def top(board: str, limit: int = 20, offset: int = 0) -> JSONResponse:
    return sent(await gateway.get(f"/query/top/{board}",
                                  {"limit": limit, "offset": offset}))


@app.get("/api/subscriber/{msisdn}")
async def subscriber(msisdn: str) -> JSONResponse:
    return sent(await gateway.get(f"/query/msisdn/{msisdn}"))


@app.get("/api/operator/{mccmnc}")
async def operator(mccmnc: str) -> JSONResponse:
    return sent(await gateway.get(f"/query/operator/{mccmnc}"))


@app.get("/api/peers/{msisdn}")
async def peers(msisdn: str, sort: str = "dur", limit: int = 100,
                offset: int = 0) -> JSONResponse:
    return sent(await gateway.get(f"/query/link/{msisdn}",
                                  {"weights": 1, "sort": sort,
                                   "limit": limit, "offset": offset}))


@app.get("/api/link/{first}/{second}")
async def link(first: str, second: str) -> JSONResponse:
    return sent(await gateway.get(f"/query/link/{first}/{second}"))


@app.get("/api/path/{first}/{second}")
async def path(first: str, second: str) -> JSONResponse:
    return sent(await gateway.get(f"/query/path/{first}/{second}", {"weights": 1},
                                  timeout=constants.PATH_TIMEOUT))


@app.get("/api/series")
async def series(metric: str = Query("records:rate"), window: str = "1h") -> JSONResponse:
    try:
        return JSONResponse(db.series(samples, metric, window))
    except KeyError:
        return JSONResponse(status_code=400,
                            content={"error": f"no such metric: {metric}",
                                     "metrics": db.metrics()})
    except ValueError as refused:
        return JSONResponse(status_code=400, content={"error": str(refused)})


@app.get("/api/metrics")
async def series_metrics() -> dict[str, Any]:
    return {"metrics": db.metrics(), "windows": [*db.WINDOWS, "all"]}


@app.get("/api/config")
async def configuration() -> JSONResponse:
    try:
        return JSONResponse(config_file.document(cfg.path))
    except OSError as failed:
        return JSONResponse(status_code=500,
                            content={"error": f"cannot read {cfg.path}: {failed}"})


@app.get("/api/system")
async def system() -> dict[str, Any]:
    health_call = await gateway.get("/query/health")
    return {"gateway": {"url": cfg.gateway_url,
                        "status": health_call.status,
                        "health": health_call.body},
            "store": {"host": cfg.store_host, "port": cfg.store_port},
            "sampler": sampler.state(),
            "routes": gateway.last(),
            "config": str(cfg.path),
            "retention_days": constants.RETENTION_DAYS}


if WEB_DIST.is_dir():
    app.mount("/", StaticFiles(directory=WEB_DIST, html=True), name="web")


if __name__ == "__main__":
    import os

    import uvicorn

    uvicorn.run(app, host=os.environ.get("UI_HOST", "127.0.0.1"), port=cfg.api_port)
