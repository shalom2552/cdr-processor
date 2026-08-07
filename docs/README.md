# CDR Processor

A distributed C++ system for processing telecom Charging Data Records.
Ingests multi-GB CDR files in parallel, aggregates them exposes the results
over an API.

## RoadMap

- [x] Phase 0 - Buildable repo skeleton & CI
- [x] Phase 1 - Core primitives, parser, config
- [x] Phase 2 - Stream & ingest CDR files
- [ ] Phase 3 - Subscriber, operator & graph aggregation
- [ ] Phase 4 - REST query gateway API
- [ ] Phase 5 - MySQL persistence across restarts
- [ ] Phase 6 - Distributed harvesters & clients
- [ ] Phase 7 - Profiling, tests & deliverables

## Prerequisites

- `g++` with C++17 and `make`
- `python3.11` or newer for the generator (it reads `config.toml` with `tomllib`)
- `pika` and a running RabbitMQ broker, only for the rabbit source
- a running Redis server, where the aggregated counters are written

The AMQP client (rabbitmq-c) and the Redis client (hiredis) are vendored at
`third_party/rabbitmq-c` and `third_party/hiredis` and built by the Makefile, so nothing
needs to be installed for the C++ side.

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

## Sources

`[source] mode` in `config.toml` picks where records come from, `file` or `rabbit`. Both
sides read it, so the one switch points the generator and the processor at the same place.
`csv` is the only supported format, and `[source.csv] separator` is the single character
its fields are split on.

The two sections below are alternatives, set up the one `mode` names.

### File Source

```toml
[source]
mode = "file"

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

Two terminals:

```bash
make gen    # writes records/ready/<timestamp>.cdr
make run    # consumes them
```

The generator renames complete `.cdr` files into the ready directory, so nothing half
written shows up. The processor watches that directory, renames a file into the
processing directory to claim it, and moves it to done, or to failed when it does not
parse. The directories are created on the first run.

**Or**

### Rabbit Source

Start a broker and install the generator's client:

```bash
docker run -d --name rabbit -p 5672:5672 -p 15672:15672 rabbitmq:3-management
# or, with rabbitmq installed on the host
sudo systemctl start rabbitmq

pip install pika            # Ubuntu/Debian
sudo pacman -S python-pika  # Arch Linux
```

```toml
[source]
mode = "rabbit"

[source.rabbit]
url       = "amqp://guest:guest@localhost/" # broker, with credentials
queue     = "cdr"                           # queue the records go through
consumers = 4                               # consumer threads, 0 for one per core
```

Two terminals:

```bash
make gen    # publishes to the queue
make run    # consumes it
```

Each record is one message on a durable queue. A consumer owns one connection and one
thread, and acks a message once it is handled, so anything left unacked is redelivered.
`scripts/consume.py` drains the queue on its own, for checking what the generator put
there without running the processor.

The `rabbitmq:3-management` image also serves the management web UI on port 15672:

```
http://localhost:15672      # user guest, password guest
```

## Store

The aggregated counters are written to Redis. Start a server:

```bash
docker run -d --name redis -p 6379:6379 redis:7
# or, with redis installed on the host
sudo systemctl start redis
```

```toml
[redis]
host       = "127.0.0.1" # redis server address
port       = 6379        # redis server port
timeout_ms = 1000        # connect and command timeout, milliseconds
```

The client (hiredis) is vendored, so only the server has to be running. Each thread opens
its own connection and pipelines its `HINCRBY` commands, and the timeout covers both the
connect and every command after it.

## Testing

This project uses the [doctest](https://github.com/onqtam/doctest) library for testing,
vendored at `third_party/doctest.h`.

To run tests:

```bash
make test
```

## Configuration

Using [tomlplusplus](https://github.com/marzer/tomlplusplus) library for configuration
parsing, vendored at `third_party/toml.h`.

Config file is located at project root `config.toml`, read once at startup and validated,
so a bad value stops the processor before any record moves.

```toml
[log]
level = "info"          # debug, info, warn, error, none

[source]
mode = "file"           # file or rabbit
format = "csv"          # csv is the only supported format

[source.csv]
separator = "|"         # one character separating the record fields

[source.file]
readers     = 4
ready_dir   = "records/ready/"
process_dir = "records/.processing"
done_dir    = "records/done"
fail_dir    = "records/failed"

[source.rabbit]
url       = "amqp://guest:guest@localhost/"
queue     = "cdr"
consumers = 4

[redis]
host       = "127.0.0.1"
port       = 6379
timeout_ms = 1000       # connect and command timeout

[generator]
rotate_seconds = 600    # seconds per .cdr file
gen_interval = 0.001    # seconds between generated records
```

Batch size is not configurable, it is `kBatchSize` in `inc/constants.hpp`.
