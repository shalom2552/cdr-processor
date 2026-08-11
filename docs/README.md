<div align="center">

![tests](https://github.com/shalom2552/cdr-processor/actions/workflows/ci.yml/badge.svg)
![C++](https://img.shields.io/badge/C%2B%2B-17-00599C)
![Python](https://img.shields.io/badge/Python-3-3776AB)
![Build](https://img.shields.io/badge/Build-Make-427819)
![Redis](https://img.shields.io/badge/Redis-7-DC382D)
![RabbitMQ](https://img.shields.io/badge/RabbitMQ-3-FF6600)
![License](https://img.shields.io/badge/License-MIT-green)

</div>

# CDR Processor

A distributed C++ system for processing telecom Charging Data Records.
Ingests multi-GB CDR files in parallel, aggregates them exposes the results
over an API.

## RoadMap

- [x] Phase 0 — Skeleton
- [x] Phase 1 — Core Infrastructure
- [x] Phase 2 — Ingest
- [x] Phase 3 — Aggregation Engine
- [x] Phase 4 — Query Gateway
- [x] Phase 5 — Persistence
- [x] Phase 6 — Distribution & Clients
- [ ] Phase 7 — Hardening & Deliverables

---

## Prerequisites

- `g++` with C++17 and `make`
- `python3.11` or newer for the generator
- `docker` and `docker compose`

Every library is vendored under `third_party/`: rabbitmq-c, hiredis, tomlplusplus and
doctest for the C++ side, pika for the generator. Nothing else to install.

## Build

Build the processor:

```bash
make build
```

Start the Redis DB:

```bash
docker compose up -d redis
```

For rabbit mode, also:

```bash
docker compose up -d rabbit
```

## Run

Generate records:

```bash
make gen
```

Run the processor (second terminal):

```bash
make run
```

Run the query gateway (third terminal):

```bash
make query
```

Stop any of them with `Ctrl-C`.

## Testing

```bash
make test
```

---

## Configuration

`config.toml` at the project root:

```toml
[log]
level = "info"          # debug, info, warn, error, none

[source]
mode   = "file"         # file or rabbit
format = "csv"          # csv is the only supported format

[source.csv]
separator = "|"         # one character separating the record fields
```

`mode` picks the source, and both sides read it.

### File Source

```toml
[source.file]
readers     = 4                     # reader threads, 0 for one per core
ready_dir   = "records/ready/"      # the generator drops .cdr files here
process_dir = "records/.processing" # claimed files, one per reader
done_dir    = "records/done"        # drained files
fail_dir    = "records/failed"      # files that did not parse
```

Paths are relative to the project root.

### Rabbit Source

```toml
[source.rabbit]
url       = "amqp://guest:guest@localhost/" # broker, with credentials
queue     = "cdr"                           # queue the records go through
consumers = 4                               # consumer threads, 0 for one per core
```

Both sides read `queue`.

### Generator

```toml
[generator]
rotate_seconds = 600    # seconds of records per .cdr file, file mode only
gen_interval   = 0.001  # seconds between records
subscribers    = 100000 # subscriber pool size, 2 or more
```

The generator feeds either mode.

### Store

```toml
[store]
type = "redis"          # redis is the only supported store
```

```toml
[redis]
host       = "127.0.0.1" # redis server address
port       = 6379        # redis server port
timeout_ms = 1000        # connect and command timeout, milliseconds
```

Both modes read the same store.

### Query Gateway

```toml
[query]
port        = 8080      # http port the query api listens on
host        = "0.0.0.0" # address the gateway binds
concurrency = 4         # handler threads, 0 for max
max_hops    = 6         # hops a path search covers before it gives up
max_visited = 10000     # subscribers a path search reads before it gives up
```

A request holds one handler thread for as long as its lookup takes.

Everything that is not configurable lives in `inc/constants.hpp`.

---

## Inspecting

### Redis

Inspect redis with `redis-cli`:

```bash
docker exec redis redis-cli hgetall total:proc            # the run totals, all 14 fields
docker exec redis redis-cli --scan --pattern 'sub:*'      # the subscriber keys
docker exec redis redis-cli hgetall sub:972500000001      # one subscriber's counters
docker exec redis redis-cli info keyspace                 # how many keys are there at all
docker exec redis redis-cli zrevrange top:voice 0 9 withscores   # the top ten by call seconds
```

Four kinds of hash:

| Key | Fields |
| --- | --- |
| `sub:<msisdn>` | `voice_out` `voice_in` `data_rx` `data_tx` `sms_out` `sms_in` `noans` `busy` `failed` |
| `op:<mccmnc>` | `voice_out` `voice_in` `sms_out` `sms_in` |
| `link:<owner>` | `<peer>:dur`, `<peer>:cnt` and `<peer>:sms`, one set per peer |
| `total:proc` | the fourteen totals fields |

Six sorted sets, written with the hashes in the same transaction:

| Key | Members | Score |
| --- | --- | --- |
| `top:voice` | subscribers | call seconds, out and in together |
| `top:sms` | subscribers | messages, out and in together |
| `top:data` | subscribers | bytes, rx and tx together |
| `top:fail` | subscribers | no-answer, busy and failed together |
| `top:op-voice` | operators | call seconds |
| `top:op-sms` | operators | messages |

### Query API

With `make query` running, on `[query] port`:

```bash
curl localhost:8080/query/msisdn/972500000001                # one subscriber's usage
curl localhost:8080/query/operator/42502                     # one operator's traffic
curl localhost:8080/query/link/972500000001                  # every peer of one subscriber
curl localhost:8080/query/link/972500000001/972500000002     # what one pair exchanged
curl localhost:8080/query/path/972500000001/972500000009     # the subscribers between two
curl localhost:8080/query/health                             # store state, key count, bounds
curl localhost:8080/query/totals                             # the store's lifetime counters
curl localhost:8080/query/top/voice                          # the highest ranked subscribers
```

Optional parameters on the three listing routes:

```bash
curl 'localhost:8080/query/link/972500000001?weights=1&sort=dur&limit=20&offset=0'
curl 'localhost:8080/query/path/972500000001/972500000009?weights=1'
curl 'localhost:8080/query/top/data?limit=50&offset=0'
```

| Parameter | Routes | Default | Meaning |
| --- | --- | --- | --- |
| `weights` | link, path | `0` | `1` adds what each peer or hop carried |
| `sort` | link | `dur` | `dur` or `sms`, the metric peers are ordered by |
| `limit` | link, top | `100`, `20` | entries returned, capped at `1000` and `500` |
| `offset` | link, top | `0` | entries skipped |

Boards for `/query/top/{board}`: `voice`, `sms`, `data`, `fail` rank subscribers,
`op-voice` and `op-sms` rank operators.


| Status | When |
| --- | --- |
| 200 | answered |
| 400 | a query parameter was refused, or no such board |
| 404 | never seen, or no such route |
| 500 | the handler threw |
| 503 | the store could not be reached |

`/query/health` never 503s, and `/query/totals` and `/query/top` never 404.

### RabbitMQ

What's in the queues:

```bash
docker exec rabbit rabbitmqctl list_queues            # queues, and what is waiting in them
docker compose logs -f rabbit                         # broker log
```

Web UI: <http://localhost:15672>, user `guest`, password `guest`.

To drains the queue and print it:

```bash
python3 scripts/consume.py
```

---

## Client Web UI

A read-only client over the gateway. It writes nothing.

```bash
make query                  # the gateway, on the host
docker compose up -d ui     # the ui, in docker
```

Web UI: <http://127.0.0.1:8000>.

```toml
[ui]
gateway_host    = "127.0.0.1" # address the gateway is reached at
api_port        = 8000        # port the ui backend listens on
sample_interval = 5           # seconds between polls of the gateway's totals
```

The container runs on the host network, so `127.0.0.1` holds either way.

| Screen | What it answers |
| --- | --- |
| Dashboard | what the store holds, and what is moving right now |
| Subscriber | one number's counters and its peers |
| Rankings | the heaviest, off the six boards |
| Graph | the contact graph around one subscriber, expandable |
| Path | the subscribers between two numbers |
| Operator | one MCCMNC, and its share of the store |
| Config | `config.toml` by section, live sections marked |
| System | gateway, store, sampler, and every route's last status |
| Settings | this browser's paging, graph and theme settings |

Every total is lifetime, since the store was created. The rates come from a sampler polling
the gateway into SQLite, so history starts when the ui does.

Outside docker, with node and the backend bare:

```bash
scripts/ui.sh               # backend, and vite
```

Web UI: <http://127.0.0.1:5173>, proxying `/api` to the backend.

---

## Make Targets

| Target | What it does |
| --- | --- |
| `make build` | builds the processor into `build/main` |
| `make run` | builds and runs the processor |
| `make query` | builds the gateway into `build/gateway` and runs it |
| `make test` | builds and runs the unit tests |
| `make gen` | runs the python generator in the configured mode |
| `make debug` | builds with `-g -O0` and the address/undefined sanitizers |
| `make release` | builds with `-O2 -DNDEBUG` |
| `make clean` | removes `build/` |


## Docker

Redis, RabbitMQ and the ui come from `docker-compose.yml`, their data on named volumes.

| Command | What it does |
| --- | --- |
| `docker compose up -d` | starts all three |
| `docker compose up -d redis` | starts redis alone |
| `docker compose up -d ui` | builds and starts the ui |
| `docker compose ps` | shows them, wait until healthy |
| `docker compose down` | stops them, the data stays |
| `docker compose down -v` | stops them and wipes the volumes |

## Project Structure

```
inc/            headers, mirrors src/
src/
  aggregate/    fold records into counters, write them as hashes and boards
  ingest/       drive work into records: dir watcher, thread pool, per-mode ingestors
  parser/       one CDR line into a CdrRecord
  query/        read side: query store, params, links, HTTP gateway
    services/   what the routes answer from: entity, path, stats, rank
  sink/         far end of ingestion: aggregate, then write
  source/       yields batches of records from a file or a queue
  store/        write side: Redis connection, hash and board writes
  util/         no app coupling: fs, json, mmap, signals, thread pool
tests/          mirrors src/, one file per class
docs/           this file, HL_Design, DL_Design, Roadmap
generator/      python CDR generator
ui/
  api/          FastAPI backend: proxy, sampler, sqlite
  web/          React web app, built by vite
scripts/        helper scripts
third_party/    vendored: rabbitmq-c, hiredis, tomlplusplus, doctest, pika
records/        input, done and failed directories at runtime
build/          objects and binaries, not tracked
```

Two binaries come out of one `src/`: `processor_main.cpp` and `gateway_main.cpp`.
Everything else links into both.

## License

MIT, see [LICENSE](../LICENSE), and `third_party/` for the vendored ones.
