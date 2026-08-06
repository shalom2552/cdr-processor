# Detailed Level Design

Core stuff lives at the top of `inc/` and `src/`. Anything bigger than one file gets a
folder of its own. Vendored libraries stay out of both and live under `third_party/`:
single headers at its top level, multi-file libraries in a folder of their own.

## CDR Record

`inc/cdr_record.hpp`.

Plain struct, one record. No methods, no parsing — the reader fills it in.

`UsageType` has eight values: `MOC` and `MTC` for outgoing and incoming calls,
`SMS_MO` and `SMS_MT` for messages, `D` for data, and `U`, `B`, `X` for calls that
went unanswered, hit a busy line, or failed.

`callTime` is the date and time together as `std::time_t`, `duration` is in seconds.
The byte counts are only used for data. The second party fields are 0 when there is
no second party.

## Parser

`inc/parser/iparser.hpp`, `inc/parser/pipe_parser.hpp`, `src/parser/pipe_parser.cpp`.

`IParser::parse()` takes a line and returns an empty optional when the line is bad.
Another format means another class behind the same interface, picked by `source.format`
in `config.toml`.

`PipeParser` reads the generator format, 12 fields split on `|`:

```
seq|imsi|imei|usage|msisdn|DD/MM/YYYY|HH:MM:SS|duration|rx|tx|sp_imsi|sp_msisdn
```

Numbers go through `std::from_chars`, so no locale and no exceptions. The byte counts
and second party fields may be empty and read as 0. Date and time are parsed by hand
and joined with `timegm`, so the result is UTC.

A line that does not fit is logged at debug level and skipped.

### Parser Factory

`inc/parser/parser_factory.hpp`, `src/parser/parser_factory.cpp`.

`ParserFactory` maps a format name to the parser that reads it. A single shared instance
registers the parsers it knows at construction, and `createParser()` builds a fresh one by
name, or returns null when the name is not registered; `hasParser()` answers without
building one. The name comes from `source.format`, so a new format is one `registerParser`
call and a class behind `IParser`, with nothing else to touch.

## Source

`inc/source/icdr_source.hpp`.

`ICdrSource::next()` fills a vector with the next batch and says `OK`, `DONE`, or `FAIL`.
Where the records come from is the implementation's business, so a RabbitMQ source later
slots in behind the same call.

### File Source

`inc/source/file_source.hpp`, `src/source/file_source.cpp`.

Every `.cdr` file opens with one line:

```
CDR|pipe|2379
```

The tag says it is a CDR file, then the format the records are written in, then how many
there are. The generator writes it and refuses to run unless
`source.format` is `pipe`, since it only knows how to write pipe records.

`FileSource` maps the file, reads that header, and hands each line to the parser. It walks
the mapping with `memchr` looking for newlines, so a line is a `string_view` into the
mapped pages and nothing is copied until the record is built. A batch stops at
`kFileBatchSize` records, and `DONE` comes when the file runs out.

A file that will not map, or that does not start with a good header, logs a warning and
yields no records. Bad lines inside a good file are skipped by the parser, one bad line
does not lose the rest.

### Rabbit Conn

`inc/source/rabbit_conn.hpp`, `src/source/rabbit_conn.cpp`.

`RabbitConn` is the AMQP plumbing on its own: it parses the url, connects, logs in, opens
one channel, sets the prefetch and starts consuming the queue. `consume` waits up to the
timeout it is given and says `OK` with the body, type and delivery tag, `TIMEOUT` when
nothing arrived, or `FAIL` when the connection is gone. `ack` tells the broker the message
is handled; messages that are never acked come back on the next connection.

Every call blocks on the socket, so a connection belongs to one thread. Opening again
closes whatever was open first, and a step that fails leaves nothing open. Nothing is
consumed before `open` succeeds, so a call on a closed connection returns a failure rather
than touching the socket. The body is copied out of the envelope once per message and the
envelope is released straight after.

### Rabbit Source

`inc/source/rabbit_source.hpp`, `src/source/rabbit_source.cpp`.

`RabbitSource` is the `ICdrSource` over a `RabbitConn`. It consumes messages and runs each
body through the parser named by `source.format`, filling a batch of up to
`kRabbitBatchSize` records. A `TIMEOUT` from the connection ends the batch early, so a
quiet queue gives back `OK` with nothing instead of blocking; a `FAIL` ends the batch with
`FAIL`. A message the parser rejects is counted and dropped, the rest of the batch goes on.

It keeps the delivery tag of the last message it took, so one `ack(tag, true)` covers a
whole batch once the records are safe. `stop()` sets an atomic flag: the batch in progress
ends at the next message and every later call says `DONE`. The connection is held by
reference and outlives the source. `parsed()` and `rejected()` are the running counts, and
the parser is built once in the constructor, which throws when the format has no parser.

## Dir Watcher

`inc/source/dir_watcher.hpp`, `src/source/dir_watcher.cpp`.

The sender writes a file somewhere else and renames it into the input directory, so a
file that appears there is already whole. `DirWatcher` puts an inotify watch on that
directory for `IN_MOVED_TO` and hands out one path at a time through `next_file`, which
blocks until something arrives. It is not thread safe and expects a thread of its own.

A file is claimed by renaming it from the input directory into the target directory.
The rename is atomic on the same filesystem, so two processes watching the same input
cannot both win the same file, and a file being worked on is no longer visible to a
watcher that starts later. A rename that fails logs a warning and the file is left alone.

Startup sweeps both directories: the target first, whose files are already claimed and
go straight on the queue, then the input, whose files are claimed as if they had just
arrived. That is what picks work back up after a crash. A watch that cannot be set up
leaves the watcher not `ok()`, and `next_file` returns false instead of blocking.

The wait is over `poll` on the inotify fd and an eventfd the watcher owns. `wake()`
writes to that eventfd, which unblocks a waiting `next_file` and makes it return false;
it is the one thread-safe entry point, so another thread can end the wait to shut down.

## Ingestor

`inc/ingest/iingestor.hpp`.

`IIngestor::start()` begins turning delivered work into records and returns false if it
could not begin; `stop()` ends it and joins whatever it started. Where the work comes from
is the implementation's business, so a queue ingestor later slots in behind the same call.

### File Ingestor

`inc/ingest/file_ingestor.hpp`, `src/ingest/file_ingestor.cpp`.

`FileIngestor` ties the directory watcher, the file source, and a thread pool together. A
feeder thread claims one file at a time from the watcher and submits it to the pool; each
worker reads the file through a `FileSource` and hands every batch to the sink. The parser
for `source.format` is built once at construction from the `ParserFactory`, so an unknown
format makes `start()` fail before any file is claimed rather than losing files later. That
one parser serves every worker, since parsing is const and holds no state.

A drained file is moved to the done directory, or to the failed directory when the source
reports a failure. `stop()` wakes the watcher, joins the feeder, and drains the pool, so no
file is left half processed.

### Ingestor Factory

`inc/ingest/ingestor_factory.hpp`, `src/ingest/ingestor_factory.cpp`.

`IngestorFactory` maps a source mode to the ingestor that drives it. A single shared
instance registers `file` and `rabbit` at construction, and `createIngestor()` builds a
fresh one by name around the sink it is given, or returns null when the mode is not
registered; `hasIngestor()` answers without building one. The name comes from
`source.mode`, so `main` never names an ingestor and a new mode is one `registerIngestor`
call and a class behind `IIngestor`.

### Rabbit Ingestor

`inc/ingest/rabbit_ingestor.hpp`, `src/ingest/rabbit_ingestor.cpp`.

`RabbitIngestor` runs `rabbit.consumers` threads over the same queue. `start()` opens one
`RabbitConn` per consumer before any thread runs, so a broker that is down makes `start()`
fail instead of leaving a running ingestor with nothing behind it; a connection that fails
on its own is logged and the remaining ones still start. An unknown `source.format` fails
`start()` first, before a socket is opened.

Each thread owns its connection and a `RabbitSource` over it, and loops until the stop flag
is set: a batch of records goes to the sink, then one `ack(last_tag, true)` covers the whole
batch, so acking costs one round trip per batch rather than per message. A read or an ack
that fails ends that thread and leaves the rest running; its messages are unacked, so the
broker redelivers them. `stop()` sets the flag, joins every thread and closes the
connections, and each thread logs what it parsed and what it rejected as it leaves.

## Sink

`inc/sink/isink.hpp`.

`ISink::consume()` takes ownership of a batch of records. It is the far end of ingestion:
the ingestor's workers call it from several threads at once, so an implementation owns its
own locking. No sink is built yet.

## Config

`inc/config.hpp`, `src/config.cpp`.

Singleton, built on first use. `load()` reads `config.toml` with toml++, `validate()`
checks the values. Both are private, so nothing can change the config later.

The header defines `inline const Config& cfg`, so any file that includes it reads
settings as `cfg.rabbit.url`.

Bad values throw and stop the program at startup.

## Logger

`inc/logger.hpp`, `src/logger.cpp`.

Singleton. Levels are `Debug < Info < Warning < Error < None`. Anything below the active
level is dropped, and `None` drops everything. The level comes from `cfg.log.level`.

Each line looks like:

```
YYYY-MM-DD HH:MM:SS [LEVEL] message
```

The time is gray and the level tag is colored: gray debug, green info, yellow warn,
red error. Write logs with `logDebug`, `logInfo`, `logWarn`, `logError`.

Output goes to stderr. A mutex keeps lines from mixing when threads log at once.

## Thread Pool

`inc/util/thread_pool.hpp`, `src/util/thread_pool.cpp`.

Fixed workers, bounded queue. Built with a worker count and a queue size, both must be
above 0 or the constructor throws.

`submit()` takes a `std::function<void()>`, pushes it, and wakes one worker. When the
queue is full it blocks until a worker frees a slot, so a fast producer cannot outrun
the workers and grow memory without bound. It returns false only when the pool is
already stopping, and true once the task is queued.

One mutex guards the queue. Two condition variables sit on it: `m_cv_full` for
producers waiting for a slot, `m_cv_empty` for workers waiting for a task. Each worker
loops: wait for a task, pop it under the lock, drop the lock, run it. The task itself
runs outside the lock, so workers do not block each other.

An exception out of a task is caught in the worker loop and logged, and the worker
takes the next task. A worker never dies.

The destructor sets the stop flag, wakes everyone, and joins. Workers only leave the
loop once the queue is empty, so queued work is drained, not dropped. The worker vector
is declared last so the threads start after the rest of the state is live.

The pool is not copyable. It is safe to submit from many threads at once, and from
inside a running task.

Tests are in `tests/util/thread_pool.cpp`. They only use the public API and every wait is
bounded, so a broken pool fails instead of hanging the suite.

## Mapped File

`inc/util/mapped_file.hpp`, `src/util/mapped_file.cpp`.

Wraps one read only `mmap`, so a 5 GB file costs no copy and no heap. The constructor
opens the path, checks it is a regular file, maps it, and tells the kernel to expect a
sequential read. The destructor unmaps.

`ok()` says whether the mapping worked, `data()` and `size()` hand the bytes to the
reader as they are. An empty file is `ok()` too, with a null pointer and size 0. Every
failure is logged with the path and leaves `ok()` false, so the caller checks once and
moves on.

The file descriptor is closed right after the map, since the mapping keeps its own
reference. Not copyable: two owners would unmap the same pages twice.
