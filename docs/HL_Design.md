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

### Parser Factory

`ParserFactory` maps a format name to a parser. It registers what it knows at startup and
builds one by name, or returns nothing when the name is unknown. The name comes from
`config.toml`, so adding a format touches nothing that already works.

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

### Rabbit Conn

The AMQP side of a queue source: connects to the broker, consumes one queue with a
prefetch, and hands back a message with its body, type and delivery tag. A message is
acked once it is handled, so anything unacked is redelivered. Calls block on the socket,
so one connection belongs to one thread.

### Rabbit Source

The record side of a queue source: consumes messages over a `RabbitConn`, parses each body,
and hands back batches like any other source. It remembers the last delivery tag so a whole
batch is acked at once after the records are safe, and a quiet queue gives back a short
batch instead of waiting. Stopping ends the batch in progress and every later call says it
is done.

### Dir Watcher

Watches the input directory with inotify and hands out one file at a time. Files arrive
by rename, so whatever shows up is complete. Claiming a file is another rename into the
work directory, which keeps two processes off the same file and survives a crash. The
blocking wait can be woken from another thread to shut the watcher down.

### Ingestor

`IIngestor` starts and stops the flow of records from a source into a sink. `FileIngestor`
drives the file path: a feeder thread claims files from the watcher and a thread pool reads
each one through a `FileSource` into the sink. The parser is chosen once at startup, so a
bad format is refused before any file moves. Drained files go to done, failures to failed.

### Sink

`ISink` is where parsed records land. Workers call `consume()` from several threads at once,
so a sink handles its own locking. None is built yet.

### Mapped File

Read only `mmap` of a whole CDR file, handed to the parser as bytes. No copy, no heap,
so file size does not turn into memory use. Bad paths fail quietly and are logged.

### Python Generator

The generator generates CDR records to three different destinations:

1. **stdout (-p, --print)**: prints the records to the terminal
2. **file (-f, --file)**: writes the records to a file headed by `CDR|<format>|<count>`
3. **rabbitmw (-r, --rabbit)**: sends the records to a RabbitMQ queue


