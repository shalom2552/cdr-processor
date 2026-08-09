# CDR Processor

A distributed C++ system for processing telecom Charging Data Records.
Ingests multi-GB CDR files in parallel, aggregates them exposes the results
over an API.

## RoadMap

- [x] Phase 0 — Skeleton
- [x] Phase 1 — Core Infrastructure
- [x] Phase 2 — Ingest
- [x] Phase 3 — Aggregation Engine
- [ ] Phase 4 — Query Gateway
- [ ] Phase 5 — Persistence
- [ ] Phase 6 — Distribution & Clients
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

## Testing

```bash
make test
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

Stop either with `Ctrl-C`.

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

[generator]
rotate_seconds = 600                # seconds of records per .cdr file
gen_interval = 0.001                # seconds between records
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

### Store

```toml
[redis]
host       = "127.0.0.1" # redis server address
port       = 6379        # redis server port
timeout_ms = 1000        # connect and command timeout, milliseconds
```

Redis is used in both modes.

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
```

Four kinds of key, each one hash:

| Key | Fields |
| --- | --- |
| `sub:<msisdn>` | `voice_out` `voice_in` `data_rx` `data_tx` `sms_out` `sms_in` `noans` `busy` `failed` |
| `op:<mccmnc>` | `voice_out` `voice_in` `sms_out` `sms_in` |
| `link:<owner>` | `<peer>:dur` and `<peer>:sms`, one pair per peer |
| `total:proc` | the fourteen totals fields |

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

## Make Targets

| Target | What it does |
| --- | --- |
| `make build` | builds the processor into `build/main` |
| `make run` | builds and runs the processor |
| `make test` | builds and runs the unit tests |
| `make gen` | runs the python generator in the configured mode |
| `make debug` | builds with `-g -O0` and the address/undefined sanitizers |
| `make release` | builds with `-O2 -DNDEBUG` |
| `make clean` | removes `build/` |


## Docker

Redis and RabbitMQ come from `docker-compose.yml`, their data on named volumes.

| Command | What it does |
| --- | --- |
| `docker compose up -d` | starts both |
| `docker compose up -d redis` | starts redis alone |
| `docker compose ps` | shows both, wait until healthy |
| `docker compose down` | stops both, the data stays |
| `docker compose down -v` | stops both and wipes the volumes |
