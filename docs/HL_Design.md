# High Level Design

CDR Processor turns call detail records into counters that can be read while the records
are still arriving. Records come from `.cdr` files on disk or from a RabbitMQ queue, are
parsed, folded into per subscriber, per operator and per pair counters, and written to
Redis as hashes and sorted sets. A second binary serves those counters over HTTP, and a
web ui reads that HTTP api. Ranking and paging are paid once per batch on the way in, so a
read is a lookup and never a scan.

## Architecture

- **Python generator**: writes synthetic CDR records to the screen, to `.cdr` files, or to
  a RabbitMQ queue. `generator/`.
- **Processor**: reads records, folds them, writes the counters. `src/processor_main.cpp`,
  built as `build/main`.
- **Query gateway**: serves the counters over HTTP under `/query`.
  `src/gateway_main.cpp`, built as `build/gateway`.
- **Redis**: holds every counter, every board and the progress of every source.
- **RabbitMQ**: the queue the records arrive on with `source.mode = "rabbit"`, unused in
  file mode.
- **UI backend**: FastAPI, one process. Proxies the gateway and samples it into SQLite.
  `ui/api`.
- **Web app**: React, reads the ui backend and nothing else. `ui/web`.

Everything but the two mains links into both binaries.

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

### Source

`ICdrSource` hands out records a batch at a time and says whether it is done or broken.
`FileSource` and `RabbitSource` sit behind it, one per source mode, so the ingestor that
drives a source names neither.

### File Source

Reads one `.cdr` file: maps it, checks the `CDR|<format>|<count>` header, and runs the
lines through the parser. A file that will not map, is empty, or carries no header yields
no records and reports itself failed. It is built with the sequence the store already
holds, so a file picked back up after a crash starts past what already landed.

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

`IIngestor` starts and stops the flow of records from a source into a sink. `start()` says
whether it began, `stop()` ends it and joins whatever it started. One implementation per
source mode, so the mode is the only thing `main` reads.

### File Ingestor

`FileIngestor` drives the file path: a feeder thread claims files from the watcher and a
thread pool reads each one through a `FileSource` into the sink. The parser is chosen once
at startup, so a bad format is refused before any file moves. A worker asks the sink where
the file resumes before opening it, so a file swept back up after a crash starts past what
already landed. Drained files go to done, failures to failed.

### Ingestor Factory

`IngestorFactory` maps `source.mode` to an ingestor and builds it around the sink, the way
`ParserFactory` maps a format to a parser. An unknown mode gives back nothing instead of a
running process. Adding a mode is a registration and a class behind `IIngestor`.

### Rabbit Ingestor

`RabbitIngestor` drives the queue path: one connection and one `RabbitSource` per consumer
thread, each parsing its own messages into the sink. A thread that cannot reach the broker
waits and tries again, doubling the wait up to a cap, so a broker that is not up yet or
restarts mid run costs a pause and not the run. A batch is acked in one call once its
records are in the sink, and anything unacked is redelivered.

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

### Rank Writer

`RankWriter` writes the boards a folded `Delta` implies: one sorted set per ranking, four for
subscribers and two for operators. Same shape as `AggregateWriter` and the same store, but
boards rather than hashes, so neither had to take on the other's job. It does not flush; the
`AggregateWriter` write that follows closes the transaction over both. Ranking is paid here,
once per batch at `O(log n)`, instead of by a scan on every read.

### Store

`IStore` is a key and field counter store: add a value, add a board score, flush what was
queued, and keep how far each source was applied so a reader can pick it back up. It carries
keys, fields, numbers and a source name, nothing about records or aggregates, so the writers
above it name no backend.

### Redis Conn

One Redis connection per thread, opened on first use and freed when the thread ends. A
connect that fails backs that thread off, doubling the wait up to a cap, so a store that is
down costs a retry rather than a stall. It knows nothing of commands, so the write side and
the read side share it as it is.

### Redis Store

The first `IStore`: one hash per key, every increment an `HINCRBY`, every board score a
`ZINCRBY`. Everything between two flushes goes out as one transaction, so a batch, its
boards and the progress that describes it land together or not at all. Commands are
pipelined, so a round trip is paid once per batch instead of once per counter, and each
thread writes on its own connection under no lock.

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
whatever `IStore` it was built with, using `AggregateWriter` for the counters and
`RankWriter` for the boards. The fold buffer is per thread
and reused, so batches cost no allocation. It also counts every batch into the `RunTotals` of
the run, logged when the run ends, and marks the highest sequence a named source reached in
the same transaction as that batch's counters, so a run killed mid file resumes without
counting anything twice.

### Query Store

`IQueryStore` is the read side of the same counters: every field of a key, the field names
alone, the fields it is named, how many keys the store holds, and one page of a board. A key
that does not exist reads as empty, and only an unreachable or rejecting server reads as a
failure.

### Redis Query

The first `IQueryStore`: one Redis read per call over the same `RedisConn` the writers use, a
board page being a `ZCARD` and a `ZREVRANGE`. It keeps no state of its own, so every handler
thread shares one instance and reads on its own connection.

### Result

The `{ status, body }` every service hands back: what to send and what to send with it. It
sits on its own so the four services and the gateway name one type.

### Query Params

The optional parameters a listing route takes, weights, sort, offset and limit, parsed off
one request, the limit clamped to a cap and anything that does not parse refused as a 400.
Paging a listing is the same function beside it. Kept out of the handlers so the rule is
written once and can be tested without a server.

### Links

Reads one subscriber's link hash, either as the peer names alone or as peers with what each
pair exchanged, and orders the weighted ones by either metric. It knows the link key and the
two field suffixes, which is why both the peer lookup and the path search read it through
here rather than each halving field names for itself.

### Query Service

Turns one entity query into store reads and a JSON body: a subscriber's usage, an operator's
traffic, a subscriber's peers, and what a pair exchanged. Peers come back as names or with
the weight of every edge, ordered and paged. Counters go out in the units they are stored
in. It knows the aggregate keys and nothing of HTTP.

### Path Service

Finds a path between two subscribers over the link hashes, searching from both parties at
once and expanding the narrower side. Two bounds from `config.toml` stop it, in hops and in
subscribers read, and a search that gives up reports both so a caller never guesses the
limit. It can also report what each hop of the path carried, which is one read per hop the
caller would otherwise make itself.

### Stats Service

Reports on the store rather than on anything in it: whether it answers, how many keys it
holds and the path bounds, and the lifetime counters of the totals hash. A store that cannot
be reached is a state it describes, not an error it fails on, so the health route always
answers.

### Rank Service

Answers one page of a ranking. It knows which boards exist and the key each is kept under,
refuses a name that is not one of them, and pages what the store hands back. Six boards:
voice, sms, data and failures by subscriber, voice and sms by operator.

### Query Factory

`QueryFactory` maps a store type to the read side of that store, the same name the writers
are registered under. It builds one by name, or returns nothing when the name is unknown, so
the gateway names no backend of its own.

### Http Gateway

The HTTP front of the query API: eight routes under `/query`, each one a service call sent
back as JSON under the status it came with. It is handed the store and builds the four
services itself, so nothing above it names them. Routes are bound in three groups, lookups,
store reports and rankings, and the listing ones parse their parameters before the service
sees them. It runs a listener and a thread pool of its own, one request per thread, so
starting it returns at once. Unknown paths and handlers that throw are answered as JSON too,
so a bad request never takes the listener down. Every answered request is logged with its
status.

### Config

Parses `config.toml` once at startup, validates it, exposes it as the global `cfg`.
Bad config throws.

### Constants

The numbers and names both binaries share: batch sizes, pipeline depth, prefetch, the
reconnect waits, and every key, field and board name the counters are written and read
under. A name written once here is why the writer and the reader cannot drift apart, and
why a tuned number is one line and not a search.

### Logger

Level-filtered logging to stderr: timestamp, colored level tag, component, message.
Level comes from `config.toml`. Thread-safe.

### Thread Pool

Fixed set of worker threads over a bounded task queue. `submit()` blocks while the
queue is full, so a fast producer is slowed down instead of piling up memory. Tasks
that throw are logged, not fatal. Shutdown drains the queue and joins the workers.

### Mapped File

Read only `mmap` of a whole CDR file, handed to the parser as bytes. No copy, no heap,
so file size does not turn into memory use. Bad paths fail quietly and are logged.

### Json

`Json` builds the bodies the query answers are sent as: fields are added one by one and come
out in the order they were added, a field being text, a number, an array of either, or an
array of objects. Names and values are escaped, so query text that came in over HTTP cannot
break the response.

### Fs

The two filesystem calls the rest of the code would otherwise repeat: create a directory and
the parents it needs, and cut a path back to its last component. Failures are logged and
reported, never thrown, since the callers already answer for themselves.

### Signal Waiter

Blocks SIGINT and SIGTERM before any thread starts, so no thread of the run takes one, then
waits for one on the main thread. Both binaries end this way: the wait returning is what
starts an orderly shutdown, outside any signal handler.

### Python Generator

The generator generates CDR records to three different destinations:

1. **stdout (-p, --print)**: prints the records to the terminal
2. **file (-f, --file)**: writes the records to a file headed by `CDR|<format>|<count>`
3. **rabbit (-r, --rabbit)**: sends the records to a RabbitMQ queue


### UI Backend

FastAPI, one process under `ui/api`. One endpoint per gateway route, same shapes, under
`/api`, so the browser stays same origin and the timeouts live in one place. It opens no
Redis connection: anything the UI needs that the gateway cannot answer becomes a gateway
route, not a store read in python. It also reads `config.toml` off disk for the config
screen, and serves the built web app.

### Sampler

A background task in the UI backend. Every `sample_interval` seconds it calls `/query/health`
and `/query/totals` and appends one row to a SQLite file: the timestamp, the fourteen
counters, the key count. That file is the only history in the system, so a curve starts when
the UI starts. Rates are derived at read time, a failed poll writes no row, and rows past
retention are swept daily.

### Web App

React and TypeScript under `ui/web`, built by Vite. Nine screens off a left rail: dashboard,
subscriber, rankings, graph, path, operator, config, system, settings. It reads and never
writes: no counter, no config, no process. Charts and the contact graph are drawn from the
data itself, in SVG and on a canvas, so the app carries no chart library. Paging, expansion
and canvas limits are the settings screen's, kept in browser storage.
