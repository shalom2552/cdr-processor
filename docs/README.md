# CDR Processor

A distributed C++ system for processing telecom Charging Data Records.
Ingests multi-GB CDR files in parallel, aggregates them exposes the results
over an API.

## RoadMap

- [x] Phase 0 - Buildable repo skeleton & CI
- [ ] Phase 1 - Core primitives, parser, config
- [ ] Phase 2 - Stream & ingest CDR files
- [ ] Phase 3 - Subscriber, operator & graph aggregation
- [ ] Phase 4 - REST query gateway API
- [ ] Phase 5 - MySQL persistence across restarts
- [ ] Phase 6 - Distributed harvesters & clients
- [ ] Phase 7 - Profiling, tests & deliverables

## Build Instructions

Run the generator first to generate CDR records.

### Generator

1. First install `pika` if using `rabbitmq`:

```bash
pip install pika # Ubuntu/Debian
# Or
sudo pacman -S python-pika # Arch Linux
```

2. Run the python generator to generate CDR records:

```bash
make gen # Using make
# Or
python3 -m generator # Using python
```

### Processor

To run the processor:

```bash
make run
```

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

Condig file is located at project root `config.toml` and contains the following parameters:

```toml
[ingestor]
file_path = "/path/to/cdr/files"
batch_size = 10000
threads = 4
```

