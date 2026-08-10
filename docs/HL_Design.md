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
`CsvParser` reads the csv format the generator writes. Bad lines are dropped and
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

### Ingestor

`IIngestor` starts and stops the flow of records from a source into a sink. `FileIngestor`
drives the file path: a feeder thread claims files from the watcher and a thread pool reads
each one through a `FileSource` into the sink. The parser is chosen once at startup, so a
bad format is refused before any file moves. A worker asks the sink where the file resumes
before opening it, so a file swept back up after a crash starts past what already landed.
Drained files go to done, failures to failed.

### Ingestor Factory

`IngestorFactory` maps `source.mode` to an ingestor and builds it around the sink, the way
`ParserFactory` maps a format to a parser. An unknown mode gives back nothing instead of a
running process. Adding a mode is a registration and a class behind `IIngestor`.

### Rabbit Ingestor

`RabbitIngestor` drives the queue path: one connection and one `RabbitSource` per consumer
thread, each parsing its own messages into the sink. Connections are opened before the
threads run, so a broker that is down is refused at startup. A batch is acked in one call
once its records are in the sink, and anything unacked is redelivered.

### Dir Watcher

Watches the input directory with inotify and hands out one file at a time. Files arrive
by rename, so whatever shows up is complete. Claiming a file is another rename into the
work directory, which keeps two processes off the same file and survives a crash. The
blocking wait can be woken from another thread to shut the watcher down.

### Delta

`Delta` is what a batch of records adds up to: counters by subscriber, by operator, and by
pair of subscribers. Plain structs, all counters start at 0. A pair is directed, so who
called whom is kept apart.

### Aggregator

`Aggregator` folds a batch of records into the `Delta` they add up to: per subscriber, per
operator, and per pair. It holds nothing between calls and touches no I/O, so threads can
fold side by side, each into its own `Delta`.

### Totals

`Totals` counts what a batch was made of, per usage type rather than per subscriber.
`RunTotals` is the same counters for the whole run, added to once per batch from any
thread. The generator counts the same fourteen fields, so what was emitted and what was
consumed can be put side by side.

### Aggregate Writer

`AggregateWriter` writes a folded `Delta` into a store: subscribers, operators, and one hash
of peers per subscriber. It also writes a batch's `Totals` under one hash of its own. It
knows the key names and nothing about the store behind them, so the same batch can be
written anywhere `IStore` is implemented.

### Store

`IStore` is a key and field counter store: add a value, flush what was queued, and keep how
far each source was applied so a reader can pick it back up. `RedisStore` is the first one,
one hash per key and every increment an `HINCRBY`. Everything between two flushes goes out
as one transaction, so a batch and the progress that describes it land together or not at
all. `RedisConn` holds the connection under it, one per thread, so the write path takes no
lock and a reader can share it.

### Store Factory

`StoreFactory` maps a store type to a store. It registers what it knows at startup and builds
one by name, or returns nothing when the name is unknown. The name comes from `config.toml`,
so a second backend is a new class and one line of registration.

### Sink

`ISink` is where parsed records land, each batch named by the source it came from. Workers
call `consume()` from several threads at once, so a sink handles its own locking, and ask
`resume_at()` how far a source got before reading it again.

### Aggregate Sink

`AggregateSink` is the first sink: it folds each batch into a `Delta` and writes it through
whatever `IStore` it was built with, using `AggregateWriter`. The fold buffer is per thread
and reused, so batches cost no allocation. It also counts every batch into the `RunTotals` of
the run, logged when the run ends, and marks the highest sequence a named source reached in
the same transaction as that batch's counters, so a run killed mid file resumes without
counting anything twice.

### Query Store

`IQueryStore` is the read side of the same counters: every field of a key, the field names
alone, or the fields it is named. `RedisQuery` is the first one, one Redis read per call over
the same `RedisConn` the writers use. A key that does not exist reads as empty, and only an
unreachable or rejecting server reads as a failure.

### Query Service

Turns one query into store reads and a JSON body: a subscriber's usage, an operator's
traffic, a subscriber's peers, what a pair exchanged, and the path between two. The path is
searched from both parties at once over the link hashes, bounded in hops and in subscribers
visited. It knows the aggregate keys and nothing of HTTP, and hands back a status and a body.

### Query Factory

`QueryFactory` maps a store type to the read side of that store, the same name the writers
are registered under. It builds one by name, or returns nothing when the name is unknown, so
the gateway names no backend of its own.

### Http Gateway

The HTTP front of the query API: five routes over digits, each one a `QueryService` call sent
back as JSON under the status it came with. It runs a listener and a thread pool of its own,
one request per thread, so starting it returns at once. Unknown paths and handlers that
throw are answered as JSON too, so a bad request never takes the listener down. Every
answered request is logged with its status.

### Mapped File

Read only `mmap` of a whole CDR file, handed to the parser as bytes. No copy, no heap,
so file size does not turn into memory use. Bad paths fail quietly and are logged.

### Json

`Json` builds the bodies the query answers are sent as: fields are added one by one and come
out in the order they were added. Names and values are escaped, so query text that came in
over HTTP cannot break the response.

### Python Generator

The generator generates CDR records to three different destinations:

1. **stdout (-p, --print)**: prints the records to the terminal
2. **file (-f, --file)**: writes the records to a file headed by `CDR|<format>|<count>`
3. **rabbitmw (-r, --rabbit)**: sends the records to a RabbitMQ queue


