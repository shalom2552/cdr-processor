# Detailed Level Design

Core stuff lives at the top of `inc/` and `src/`. Anything bigger than one file gets a
folder of its own. Vendored libraries stay out of both and live under `third_party/`:
single headers at its top level, multi-file libraries in a folder of their own.

## Design decisions

A link is written in both directions from the one record that mentions the pair: owner to
peer and peer to owner. The generator emits every record on its own, so the other leg of a
call never arrives, and a peer that is never a subscriber still gets a hash of its own. A
feed that carries both legs would count each call twice.

A record with no subscriber MSISDN counts nowhere, and one with no second party gets no
link.

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

`inc/parser/iparser.hpp`, `inc/parser/csv_parser.hpp`, `src/parser/csv_parser.cpp`.

`IParser::parse()` takes a line and returns an empty optional when the line is bad.
Another format means another class behind the same interface, picked by `source.format`
in `config.toml`.

`CsvParser` reads the generator format, 12 fields split on the separator from
`source.csv.separator`, `|` by default:

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
CDR|csv|2379
```

The tag says it is a CDR file, then the format the records are written in, then how many
there are. The generator writes it and refuses to run unless
`source.format` is `csv`, since it only knows how to write csv records.

`FileSource` maps the file, reads that header, and hands each line to the parser. It walks
the mapping with `memchr` looking for newlines, so a line is a `string_view` into the
mapped pages and nothing is copied until the record is built. A batch stops at
`kFileBatchSize` records, and `DONE` comes when the file runs out.

A file that will not map, or that does not start with a good header, logs a warning and
yields no records. Bad lines inside a good file are skipped by the parser, one bad line
does not lose the rest.

The constructor also takes a resume sequence, 0 by default. Every parsed record whose
sequence is at or below it is dropped before the batch is filled, so a file picked back up
after a crash restarts where it left off instead of at record 1. Sequences are unique
within a file and the file is read in order, so one number describes the whole of what was
already applied. The skip is per record, not per offset, so a file that lost a line to the
parser still resumes at the right place.

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

A worker asks the sink where the file resumes before it opens it, keyed by the file's own
name, and hands that name back with every batch. A file the watcher swept up after a
`kill -9` is therefore read from the first record the store never saw, and the records that
already landed are not counted twice.

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

### Dir Watcher

`inc/ingest/dir_watcher.hpp`, `src/ingest/dir_watcher.cpp`.

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

## Delta

`inc/aggregate/delta.hpp`.

Header only, plain structs. A `Delta` is what one batch of records adds up to, held in
three maps: `subs` by subscriber IMSI, `ops` by operator code, `links` by pair of
subscribers. Every counter starts at 0, so a first record can be added into a fresh entry
without a lookup first.

`SubDelta` counts call seconds each way, bytes each way, messages each way, and the calls
that went unanswered, busy, or failed. `OpDelta` keeps only voice and sms. `LinkDelta`
keeps the seconds and messages one pair exchanged.

`LinkKey` is directed: owner to peer and peer to owner are two entries. `LinkHash` runs
each half through a splitmix64 finalizer and combines them with an offset, so swapping
them changes the hash.

A subscriber entry is 72 bytes of counters plus the map's node, so a run over millions of
subscribers is measured in hundreds of megabytes.

## Aggregator

`inc/aggregate/aggregator.hpp`, `src/aggregate/aggregator.cpp`.

`fold()` walks a batch once and adds it into a `Delta`, which it clears first and whose
buckets it reuses across calls. Nothing is kept between calls, so any number of threads can
fold at the same time as long as each holds its own `Delta`.

Each record adds to the subscriber's bucket, keyed by MSISDN. Calls add their seconds each
way, messages count one each way, data adds its byte counts, and unanswered, busy and
failed calls each get their own counter. The operator bucket is keyed by the MCCMNC of the
subscriber's own IMSI and takes only voice and sms; an IMSI too short to hold one counts
nowhere. Calls and messages also add to the link between the two parties.

One record is a handful of map lookups and integer adds, so the cost of a batch is the
hashing, not the arithmetic.

## Totals

`inc/aggregate/totals.hpp`, `src/aggregate/totals.cpp`.

Fourteen counters, flat for the whole run instead of the per subscriber buckets of a
`Delta`. Named after the usage types, since the generator counts the same fourteen and has
no subscribers to key them by. `data_dur` is counted although the aggregates never use it.

`Totals` is a plain struct. `add()` counts one record or a batch, before any check a
`Delta` makes, so a record without a subscriber MSISDN still counts. `format()` renders
the block as the tail of one log message, a tab then the name padded then the value.

`RunTotals` is the same counters as atomics: `merge()` folds a batch in with one relaxed
`fetch_add` per non-zero counter, `snapshot()` reads them back. No record touches an
atomic, and a snapshot taken mid merge can hold part of a batch.

## Aggregate Writer

`inc/aggregate/aggregate_writer.hpp`, `src/aggregate/aggregate_writer.cpp`.

Turns a folded `Delta` into store calls. Subscribers go to `sub:<msisdn>`, operators to
`op:<mccmnc>`, links to `link:<owner>` with `<peer>:dur` and `<peer>:sms` as fields, so a
subscriber's peers are one hash and not one key per edge. Counters that are 0 are skipped
rather than written, since the batch never touched them.

It holds nothing but the store it writes to, so threads can share one writer if the store
allows it. The key and field are built into two buffers that are reused down the whole
delta, so a batch costs no allocation past the first entry. The store is flushed once at
the end and the batch is reported failed if any counter or the flush failed.

A second overload writes a batch's `Totals` to `total:proc`, fourteen fields at most. It
queues and does not flush: it runs before the delta write, which drains both. The other
way round the totals wait for the next batch and the last batch never goes out.

The hash sums every run, the block logged at shutdown is one run, so a comparison starts
with `redis-cli del total:proc`.

## Rank Writer

`inc/aggregate/rank_writer.hpp`, `src/aggregate/rank_writer.cpp`.

Turns the same folded `Delta` into board calls. A subscriber scores on four boards —
`top:voice` from its two call durations, `top:sms` from its two message counts, `top:data`
from its two byte counters, `top:fail` from no-answer, busy and failed together — and an
operator on `top:op-voice` and `top:op-sms`. The member is the msisdn or the mccmnc, so a
board hands back the same id the lookup routes take. A score of 0 is skipped, the way a
counter of 0 is.

It does not flush. It runs before the delta write, which drains what both of them queued, so
a batch's hashes and its boards commit in one transaction or not at all. The member buffer is
reused down the whole delta, so the pass costs no allocation past the first entry.

Four `ZINCRBY` per subscriber and two per operator on top of nine and four `HINCRBY`, all
pipelined and none of them read: about forty percent more commands for a ranking that would
otherwise cost an `HGETALL` per key on every read. Two ranking questions a sorted set cannot
answer are ratios, which are not incrementally maintainable, and peer count, which needs the
`HINCRBY` reply the pipeline discards.

## Store

`inc/store/istore.hpp`.

`IStore::increment()` adds a value to one field of one key, `rank()` adds a score to one
member of one board, `flush()` completes everything queued so far. `resume_at()` reads how
far one source was already applied and `mark()`
queues the new high-water mark without completing it, so the mark goes out with the
increments it belongs with rather than ahead of them. All of them are called from several
threads at once, so an implementation owns its own locking or gives each thread its own
state. Nothing about records or aggregates reaches this far: it is keys, fields, numbers
and a source name.

### Redis Conn

`inc/store/redis_conn.hpp`, `src/store/redis_conn.cpp`.

One context per thread, opened on first use and freed when the thread ends. `get()` opens
it, and opens a new one when the old broke; `peek()` returns it without opening; `drop()`
frees it. Host, port and timeout come from `config.toml`, and the timeout covers the
connect and every command after it. It knows nothing about pipelines or commands, so a
read side reuses it as it is.

### Redis Store

`inc/store/redis_store.hpp`, `src/store/redis_store.cpp`.

One hash per key, every increment an `HINCRBY` appended to a hiredis pipeline on this
thread's `RedisConn` context, so no lock is taken on the write path. `rank()` appends a
`ZINCRBY` into the same pipeline and the same open batch, so a board is closed by the flush
that closes the counters beside it. The pipeline depth is the store's own, one counter per
thread.

Commands go out without waiting for their replies. The pipeline drains itself once it holds
`kRedisPipelineDepth` commands, and `flush()` drains the rest, reading one reply per
command. A reply that carries an error is counted as a failure and logged, so a rejected
increment is not read as a write. A broken connection is dropped and opened again on the
next call, and the commands that were queued on it are lost and reported.

Everything between two flushes is one transaction. A `MULTI` is queued lazily, by the first
command that follows a flush, and `flush()` closes it with an `EXEC`, so nothing above the
store has to open or commit anything and the interface stays two writes and a flush. The
depth drain reads the `+QUEUED` replies the server answers inside a transaction with, so a
batch larger than the pipeline still goes out in one commit rather than several. Redis
truncates a trailing partial `MULTI` when it loads the AOF, so a batch killed halfway lands
whole or not at all.

`resume_at()` is one `HGET` of the source's field of `prog:file`, a field that does not
exist reading as 0. It flushes first, so no reply of this thread's sits ahead of the one it
waits for. `mark()` is one `HSET` into the open batch, which is what makes the mark and the
counters it describes commit together: written before them, a crash between the two would
lose records for good.

Cost per increment is the append into the output buffer; the round trip is paid once per
1024 commands instead of once each.

### Store Factory

`inc/store/store_factory.hpp`, `src/store/store_factory.cpp`.

`StoreFactory` maps a store type to the store that holds the counters. A single shared
instance registers the stores it knows at construction, one line per backend, and
`createStore()` builds a fresh one by name, or returns null when the name is not registered;
`hasStore()` answers without building one. The name comes from `store.type`, so a new
backend is one `registerStore` call and a class behind `IStore`, with nothing else to touch.

## Sink

`inc/sink/isink.hpp`.

`ISink::consume()` takes ownership of a batch of records and the name of the source they
came from, empty when that source cannot be resumed. `resume_at()` answers how far a source
was already consumed, 0 by default, so a sink that keeps no progress needs nothing of it.
It is the far end of ingestion: the ingestor's workers call it from several threads at
once, so an implementation owns its own locking.

### Aggregate Sink

`inc/sink/aggregate_sink.hpp`, `src/sink/aggregate_sink.cpp`.

Holds an `Aggregator`, the `IStore` it was built with, and an `AggregateWriter` and a
`RankWriter` over that store, so it names no backend and a null store is refused at
construction. `consume()` folds the batch into a `Delta` and a `Totals`, merges the totals
into the run's `RunTotals`, then writes the totals, the boards and the counters in that
order. `snapshot()` hands the run's counters back, records that reached nothing included.
`resume_at()` is the store's own answer, passed through.

A batch that came with a source name also has the highest sequence it held marked, queued
between the totals and the board write, so the mark rides the flush the delta write ends
with and commits in the same transaction as the counters. A batch with no source name, or
one that holds nothing, marks nothing. Only the delta write flushes, so the totals, the
mark, the boards and the counters are one commit.

The `Delta` is one per thread and lives past the call, so its buckets are reused batch after
batch and a steady stream allocates nothing. The `Totals` is one per call, on the stack.
Nothing is locked: the fold reads only the batch, the merge is fourteen relaxed atomic adds,
and the store gives each thread its own connection. A batch that did not fully land is
logged by the writer and dropped.

## Query Store

`inc/query/iquery_store.hpp`.

The read side of the same counters. `hgetall()` reads every field of one key, `hkeys()` the
field names alone, `hmget()` the fields it is named, `dbsize()` how many keys the store
holds, and `top()` one page of a board with the board's cardinality beside it. Each one
clears the output first and returns false only when the store could not be reached, so a key
that does not exist is a success with nothing in it. `Ranked` sits beside `Fields` in the
same shape, a vector of pairs owned by the caller. Calls come from several threads at once,
and the interface carries keys, fields, members and numbers, nothing about records or
aggregates.

## Redis Query

`inc/query/redis_query.hpp`, `src/query/redis_query.cpp`.

`HGETALL`, `HKEYS`, `HMGET` and `DBSIZE` over this thread's `RedisConn` context, one round
trip per call, no lock and no state of its own. Keys and field names go out as lengths and
bytes, so a `string_view` over a larger buffer is read as it stands. Values come back as
strings, and an element that is not a string reads as empty.

`top()` is the one call that costs two round trips: a `ZCARD` for the board's cardinality,
then `ZREVRANGE start stop WITHSCORES` for the page, `-1` as the stop when no limit was
asked for. A score arrives as text because Redis keeps it as a double and prints it with
`%.17g`, so it is read with `strtod` and truncated. That caps an exact score at 2^53, which
no counter here reaches.

A broken connection is logged and dropped, so the next call opens a new one, and the read
returns false. A reply that carries an error is logged and also returns false, so a rejected
read is never read as an empty key. `hmget()` with no field names asks nothing and succeeds.

Cost per call is one round trip plus one allocation for the reply and one per value kept.

## Result

`inc/query/result.hpp`.

The `{ int status; std::string body; }` every service hands back. Four lines of its own so
the four services and the gateway name one type rather than one service's nested one.

## Query Params

`inc/query/query_params.hpp`, `src/query/query_params.cpp`.

`QueryParams` is the four optional parameters a listing route takes: `weights`, `sort`,
`offset` and `limit`. `parseParams()` fills it from an `httplib::Request` and hands back a
`Result` — 200 with an empty body when it parsed, 400 naming the parameter when it did not.
`weights` takes only `0` or `1`, `sort` only `dur` or `sms`, and `offset` and `limit` must
be a whole number end to end, so `10x` is refused rather than read as 10. A request that
names no limit is given the fallback its route passes, and a limit over the cap is clamped
to it and reported clamped. A limit of 0 asks for every entry, which is what a direct caller
that fills the struct itself gets.

`page()` beside it cuts a vector to the offset and limit, empty when the offset is past the
end. Both live here rather than in a handler so the rule is written once and can be tested
without a running server. The board name is not this file's business: it arrives as a path
element, and `RankService` is what knows which boards exist.

## Links

`inc/query/links.hpp`, `src/query/links.cpp`.

The link hash read two ways. `link_peers()` is `HKEYS` on `link:<msisdn>` with the `:dur`
and `:sms` suffix dropped, sorted and deduplicated, which is the cheap read the path search
wants. `link_weights()` is `HGETALL` on the same key with a peer's two fields folded into
one `Peer`, which is what the weighted route wants: a hub costs one read of every field, and
that is the price of ranking its peers correctly rather than returning an arbitrary slice.
`order_peers()` sorts by either metric descending, ties by the other metric and then by
msisdn, so paging is stable.

Both take the store as an argument and keep nothing, so `QueryService` and `PathService`
read the link hash through one place instead of each halving field names for itself.

## Query Service

`inc/query/services/query_service.hpp`, `src/query/services/query_service.cpp`.

Answers the entity queries out of an `IQueryStore`. `msisdn()` and `op()` read one hash whole
and rename its fields to the response names; the byte counters go out as bytes, the unit they
are stored in. `link()` reads the two fields of one pair. A read that failed is answered 503,
a key with no fields 404.

`peers()` is the one that takes parameters. Without `weights` it is `link_peers()` paged, the
order still msisdn ascending, plus the count of every peer whether returned or not. With
`weights` it is `link_weights()` ordered by the chosen metric and paged, each peer carrying
its duration and its message count. Both report `count`, `offset` and `limit`, so a caller
can page without a second call.

Nothing is kept between calls but the store reference, so one instance serves every handler
thread the store is safe for.

## Path Service

`inc/query/services/path_service.hpp`, `src/query/services/path_service.cpp`.

`path()` runs a breadth first search from both parties at once, expanding the narrower
frontier each round so the search stays off the hubs, and joins the two trails at the
subscriber they share. One store read per subscriber expanded, and the search gives up after
`query.max_hops` rounds or `query.max_visited` subscribers. The 404 body carries both bounds,
so a caller states the limit rather than guessing it.

With `weights` it resolves the path it already holds: one `HMGET` per hop after the search,
bounded by the hop limit. A hop that reads empty reports zeros rather than failing — the link
exists, the search walked it. Doing it here is one pass; from a caller it is one round trip
per hop of a path it was just handed.

## Stats Service

`inc/query/services/stats_service.hpp`, `src/query/services/stats_service.cpp`.

`health()` is one `dbsize()`, which proves the connection and returns the key count in the
same round trip. It always answers 200: the route reports on the gateway, and the gateway
answered, so an unreachable store is `"store":"down"` with `"keys":0` rather than a 503. The
path bounds go out with it, read from config.

`totals()` is one `HGETALL` on `total:proc`, the fourteen fields written out under the stored
names with underscores turned into hyphens. A missing or empty hash is 200 with every field
0, not 404: a store that has processed nothing is a real state, and unlike a subscriber the
store itself always exists. Only an unreachable store is 503. The counters are lifetime, every
run since the hash was last cleared, and nothing here invents a run identity.

## Rank Service

`inc/query/services/rank_service.hpp`, `src/query/services/rank_service.cpp`.

`top()` maps a board name to its key — `voice`, `sms`, `data`, `fail`, `op-voice`, `op-sms` —
refuses anything else with a 400, and hands the offset and limit to `IQueryStore::top()`. The
store does the paging, so the cost is `O(log n + k)` and no key is scanned. The response
carries the board, its whole cardinality, the offset and limit it was served under, and the
page as `{ id, score }` objects. An empty board is 200 with no entries, never 404.

## Query Factory

`inc/query/query_factory.hpp`, `src/query/query_factory.cpp`.

`QueryFactory` maps a store type to the read side of that store. A single shared instance
registers what it knows at construction, one line per backend, and `createQuery()` builds a
fresh one by name, or returns null when the name is not registered; `hasQuery()` answers
without building one. The name is the same `store.type` the writers use, so both sides of one
backend are registered together and the gateway names no class of its own.

## Http Gateway

`inc/query/http_gateway.hpp`, `src/query/http_gateway.cpp`.

The HTTP front of the query API, cpp-httplib behind it. It takes the `IQueryStore` and holds
the four services by value, so nothing above it constructs or names them; they are stateless
and cost a reference each. The constructor builds the server and calls
`registerQueryRoutes()`, `registerStatsRoutes()` and `registerRankRoutes()`, which bind eight
routes between them, each one handing its captures to a service call and sending what comes
back as `application/json` under the status it came with. Three of them run `parseParams()`
first and send its 400 unchanged, so a bad parameter never reaches a service. A path that
matches no route answers 404 with a JSON body, and a handler that throws is logged and
answered 500, so a failing query never takes the listener down. Every request that was
answered logs its method, path and status, unknown routes included.

| Route | Answers |
|---|---|
| `GET /query/msisdn/{n}` | nine counters, bytes as bytes |
| `GET /query/operator/{mccmnc}` | four counters |
| `GET /query/link/{n}` | peers, optionally weighted, sorted and paged |
| `GET /query/link/{a}/{b}` | duration and sms |
| `GET /query/path/{a}/{b}` | path, optionally per-hop weights |
| `GET /query/health` | gateway and store state, key count, path bounds |
| `GET /query/totals` | the store's fourteen lifetime counters |
| `GET /query/top/{board}` | one page of a ranking, six boards |

200 with a body, 400 on a bad parameter, 404 for an entity never seen, 503 when the store is
unreachable, 500 when a handler throws, JSON on every one. `/query/health` never 503s, and
`/query/totals` and `/query/top` never 404.

`start()` binds `query.port` and serves it on a thread of its own, so it returns as soon as
the port is taken and false when it is not. `stop()` ends the listener and joins that thread,
and the destructor calls it, so a gateway that goes out of scope leaves nothing running.
Requests are served by a thread pool of `query.concurrency` threads, one request at a
time each, and the store gives every thread its own connection, so nothing is locked between
handlers. The response body is moved into the response, so a query costs its store reads and
one JSON buffer.

## Config

`inc/config.hpp`, `src/config.cpp`.

Singleton, built on first use. `load()` reads `config.toml` with toml++, `validate()`
checks the values. Both are private, so nothing can change the config later. `query.max_hops`
and `query.max_visited` live here rather than in `constants.hpp` because they are the two
numbers worth tuning per deployment and the API states them on screen.

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

## Json

`inc/util/json.hpp`, `src/util/json.cpp`.

Builder of a JSON object for the query responses. `add()` takes a string, a number, a vector
of strings or a vector of `Json`, and appends the field to one buffer, so the fields come out
in the order they were added and `str()` only wraps that buffer in braces. The vector of
`Json` is what the weighted peers, the path hops and the board entries are built from: each
element is a whole object, so one nesting level costs no parser and no tree. `error()` builds
the one field object the failing queries answer with.

Every name and every string value is written through `quoted()`, which escapes quotes,
backslashes and control characters, so text that came in over HTTP cannot break the body.
That is one pass over the bytes per string; other bytes are copied as they are. Nothing is
parsed and nothing is validated, so a caller that adds the same name twice writes it twice.

## UI Backend

`ui/api/`. FastAPI, uvicorn, httpx, sqlite3. Modules are imported by plain name, so the
process runs with `ui/api` on the path: `uvicorn main:app --app-dir ui/api`, or
`python api/main.py` in the container.

### Settings

`ui/api/settings.py`.

Reads `config.toml` once at startup with `tomllib`: `[ui]` for `gateway_host`, `api_port` and
`sample_interval`, `[query] port` for the gateway port, `[redis]` for the address the system
screen reports. The file is the one `CDR_CONFIG` names, else the nearest one walking up from
the working directory. `[query] host` is a bind address and is deliberately not reused as a
client address, which is why `[ui] gateway_host` exists. The container runs on the host
network, so that address is `127.0.0.1` in docker and out of it, and `api_port` is the port
bound rather than a port mapped. Everything else the backend runs on is in
`ui/api/constants.py`; everything the user tunes per view is in the browser.

### Gateway Client

`ui/api/gateway.py`.

One `httpx.AsyncClient` against `http://{gateway_host}:{query.port}`. `get()` never raises: a
connection failure is 502, an overrun timeout is 504, and any status the gateway sent passes
through with its body. Each call records how that route last answered, which is what the
system screen lists. The path route is given the longer timeout, the only one that needs it.

### Samples

`ui/api/db.py`.

One SQLite table, `ts` primary key with the key count and the fourteen counters as columns.
A connection per call: a row every few seconds and a few hundred rows a read is nothing worth
pooling. `append()` writes what the gateway sent and zero for what it did not. `sweep()`
drops rows past `RETENTION_DAYS`, `stats()` reports the file for the system screen.

`series()` derives what a chart reads. A metric is one of the fourteen, the key count, a sum
of counters (`calls`, `messages`, `data-vol`, `failures`), or a ratio (`fail-share`, the three
averages). A `:rate` suffix asks for `(value - previous) / seconds` between samples instead of
the counter; a ratio has no rate and is refused. Counters only rise, so a negative delta means
the store was flushed and clamps to zero: the chart carries on flat rather than spiking, at
the cost of a reset looking like a quiet minute. The window reads one row before it so the
first rate has a baseline, and the result is thinned to `MAX_POINTS` by keeping the last point
of each bucket.

### Sampler

`ui/api/sampler.py`.

An asyncio task started with the app. Each tick calls `/query/health` and `/query/totals`
together and appends one row. A failed call, or a store that reports down, writes nothing and
is counted: a gap in the data is the truth about that minute. It keeps the cadence when
nothing moved rather than skipping the row, so a flat line means idle and only a gap means
failure. Retention is swept once a day off the same tick. SQLite writes go through
`asyncio.to_thread`, so the event loop never blocks on the file.

### Config Document

`ui/api/config_file.py`.

Reads `config.toml` twice over: `tomllib` for the values, and a line scan for the shape the
config screen shows — sections in file order, the comment block written above each one as its
help, and the trailing comment on each key. Banner rules reset the block, so a section carries
its own prose and not the file header's. `source.mode`, `source.format` and `store.type` say
which sections are live; the rest are marked inactive with the setting that turned them off.
It reports the file, not any running process.

### Routes

`ui/api/main.py`.

Eight proxy endpoints, one per gateway route, adding nothing but the timeout and the status.
`/api/series` and `/api/metrics` answer off SQLite, `/api/config` off the file, `/api/system`
folds the gateway health, the store address, the sampler state and the last status of every
route the UI has called. The built web app is mounted at `/` when `ui/web/dist` exists, so the
browser is same origin with the API and never sees the gateway.

## Web App

`ui/web/`. React, TypeScript, Vite. Two runtime dependencies, react and react-dom: charts are
SVG written for this app and the contact graph is a canvas force simulation, so no chart or
graph library is vendored.

`lib/api.ts` is the only place a URL is written, and it turns a failing response into an
`ApiError` carrying the status. That status is what the screens render on: 502 the gateway is
down, 503 the store is, 504 a search timed out, 404 never seen. The three failures never
render alike. `lib/format.ts` is the only place seconds, bytes, counts and shares are
formatted. `lib/settings.ts` holds the browser's settings — theme, refresh, default window,
peer page, board page, expand limit, canvas cap, edge metric — in local storage behind
`useSyncExternalStore`, and `lib/history.ts` holds recent lookups the same way. Routing is the
URL hash, so a screen is a link and the static mount needs no fallback.

The graph screen keeps its nodes and edges in a ref and steps a repulsion, spring and centring
simulation on `requestAnimationFrame`. A node's size is the sum of its **known** edges, the
ones fetched rather than all it has, which the legend says out loud. An expansion pulls
`expandLimit` peers heaviest first and says "showing 50 of 812" when there are more; at the
canvas cap an expansion is refused with the reason instead of freezing.
