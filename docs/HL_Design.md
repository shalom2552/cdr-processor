# High Level Design

This document outlines the high-level design of this project.
It provides an overview of the architecture, key components, and overall functionality.

## Architecture

The project is structured as follows:
- **Python Generator**: Generates CDR records.
- **C++ Reducer/Digestor**: Processes and analyzes CDR records.
- **Database**: Stores and retrieves processed data.
- **API**: Provides access to the database.


### CDR Record

`CdrRecord` holds one call record in memory: subscriber ids, usage type, time,
duration, and byte counts. `UsageType` covers voice, SMS, data, and the calls that
never went through.

### Parser

`IParser` is the interface: a line goes in, a `CdrRecord` or nothing comes out.
`PipeParser` reads the pipe format the generator writes. Bad lines are dropped and
logged.

### Config

Parses `config.toml` once at startup, validates it, exposes it as the global `cfg`.
Bad config throws.

### Logger

Level-filtered logging to stderr: timestamp, colored level tag, message.
Level comes from `config.toml`. Thread-safe.

### Thread Pool

Fixed set of worker threads over a bounded task queue. `submit()` blocks while the
queue is full, so a fast producer is slowed down instead of piling up memory. Tasks
that throw are logged, not fatal. Shutdown drains the queue and joins the workers.

### Source

`ICdrSource` hands out records a batch at a time and says when it is done. `FileSource`
is the first one: it maps a `.cdr` file, checks the `CDR|<format>|<count>` header, and
runs the lines through the parser. A queue source drops in behind the same interface.

### Dir Watcher

Watches the input directory with inotify and hands out one file at a time. Files arrive
by rename, so whatever shows up is complete. Claiming a file is another rename into the
work directory, which keeps two processes off the same file and survives a crash.

### Mapped File

Read only `mmap` of a whole CDR file, handed to the parser as bytes. No copy, no heap,
so file size does not turn into memory use. Bad paths fail quietly and are logged.

### Python Generator

The generator generates CDR records to three different destinations:

1. **stdout (-p, --print)**: prints the records to the terminal
2. **file (-f, --file)**: writes the records to a file headed by `CDR|<format>|<count>`
3. **rabbitmw (-r, --rabbit)**: sends the records to a RabbitMQ queue


