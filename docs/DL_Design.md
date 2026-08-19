# Detailed Level Design

Core at the top of `inc/` and `src/`. Anything bigger than one file gets a folder. Vendored
libraries under `third_party/`: single headers at its top level, multi-file libraries in a
folder of their own.

## Design decisions

- A link is written both ways from the one record naming the pair: owner to peer, peer to
  owner. The generator emits each record alone, so the other leg never arrives, and a peer
  that is never a subscriber still gets a hash. A feed carrying both legs would double count.
- No subscriber MSISDN: counts nowhere. No second party: no link.

## CDR Record

`inc/cdr_record.hpp`.

- Plain struct, one record. No methods, no parsing; the reader fills it.
- `UsageType`, eight values: `MOC`/`MTC` outgoing/incoming calls, `SMS_MO`/`SMS_MT`
  messages, `D` data, `U`/`B`/`X` unanswered/busy/failed.
- `callTime` is date and time as `std::time_t`, `duration` in seconds. Byte counts are data
  only. Second party fields are 0 when there is no second party.

## Parser

`inc/parser/iparser.hpp`, `inc/parser/csv_parser.hpp`, `src/parser/csv_parser.cpp`.

- `IParser::parse()`: line in, empty optional when bad. Another format is another class
  behind the interface, picked by `source.format`.
- `CsvParser`: generator format, 12 fields split on `source.csv.separator`, `|` default.

```
seq|imsi|imei|usage|msisdn|DD/MM/YYYY|HH:MM:SS|duration|rx|tx|sp_imsi|sp_msisdn
```

- Numbers via `std::from_chars`: no locale, no exceptions.
- Byte counts and second party fields may be empty, read as 0.
- Date and time parsed by hand, joined with `timegm`, so UTC.
- A line that does not fit: logged at debug, skipped.

### Parser Factory

`inc/parser/parser_factory.hpp`, `src/parser/parser_factory.cpp`.

Format name to parser. One shared instance registers at construction. `createParser()`
builds a fresh one by name, null when unregistered; `hasParser()` answers without building.
Name from `source.format`, so a new format is one `registerParser` call and a class behind
`IParser`.

## Source

`inc/source/icdr_source.hpp`.

`ICdrSource::next()` fills a vector with the next batch and says `OK`, `DONE`, or `FAIL`.
`FileSource` reads a mapped file, `RabbitSource` a queue.

### File Source

`inc/source/file_source.hpp`, `src/source/file_source.cpp`.

Every `.cdr` file opens with one line:

```
CDR|csv|2379
```

Tag, record format, count. The generator writes it and refuses to run unless
`source.format` is `csv`, the only format it writes.

- Maps the file, reads the header, hands each line to the parser. Walks the mapping with
  `memchr` for newlines, so a line is a `string_view` into the mapped pages and nothing is
  copied until the record is built.
- Batch stops at `kFileBatchSize`. `DONE` when the file runs out.
- Will not map, no bytes, or bad header: warning logged, no records, first `next()` answers
  `FAIL`, which routes the file to the failed directory.
- Bad lines inside a good file are skipped by the parser; one bad line does not lose the
  rest. A drained file logs one summary: parsed, rejected, elapsed.
- Constructor takes a resume sequence, 0 default. Parsed records at or below it are dropped
  before the batch fills, so a file picked back up after a crash restarts where it stopped.
  Sequences are unique within a file and the file is read in order, so one number describes
  all that was applied. The skip is per record, not per offset, so a file that lost a line to
  the parser still resumes right.

### Rabbit Conn

`inc/source/rabbit_conn.hpp`, `src/source/rabbit_conn.cpp`.

- AMQP plumbing alone: parses the url, connects, logs in, opens one channel, sets the
  prefetch, starts consuming.
- `consume` waits up to its timeout: `OK` with body, type and delivery tag, `TIMEOUT` when
  nothing arrived, `FAIL` when the connection is gone.
- `ack` marks a message handled; anything never acked comes back on the next connection.
- Every call blocks on the socket, so a connection belongs to one thread.
- Opening again closes what was open; a failed step leaves nothing open. Nothing is consumed
  before `open` succeeds, so a call on a closed connection fails without touching the socket.
- Body copied out of the envelope once per message, envelope released straight after.

### Rabbit Source

`inc/source/rabbit_source.hpp`, `src/source/rabbit_source.cpp`.

- The `ICdrSource` over a `RabbitConn`. Each body through the parser named by
  `source.format`, up to `kRabbitBatchSize` records.
- `TIMEOUT` ends the batch early, so a quiet queue answers `OK` with nothing instead of
  blocking. `FAIL` ends the batch `FAIL`. A rejected message is counted and dropped, the rest
  of the batch goes on.
- Keeps the last delivery tag, so one `ack(tag, true)` covers a batch once its records are
  safe.
- `stop()` sets an atomic flag: the batch in progress ends at the next message, later calls
  say `DONE`. `RabbitIngestor` does not use it, it ends its own loop and drops the source, so
  the batch in progress is left unacked and redelivered.
- Connection held by reference, outlives the source. `parsed()`/`rejected()` are running
  counts. Parser built once in the constructor, which throws when the format has no parser.

## Ingestor

`inc/ingest/iingestor.hpp`.

`start()` begins turning delivered work into records, false if it could not begin; `stop()`
ends it and joins. `FileIngestor` drives a watched directory, `RabbitIngestor` a queue.

### File Ingestor

`inc/ingest/file_ingestor.hpp`, `src/ingest/file_ingestor.cpp`.

- Watcher, file source and thread pool tied together. A feeder thread claims one file at a
  time and submits it to the pool; each worker reads it through a `FileSource` into the sink.
- Parser for `source.format` built once at construction from `ParserFactory`, so an unknown
  format fails `start()` before any file is claimed rather than losing files later. One
  parser serves every worker, since parsing is const and stateless.
- A worker asks the sink where the file resumes before opening it, keyed by the file's own
  name, and hands that name back with every batch. A file swept up after a `kill -9` is read
  from the first record the store never saw, and records already landed are not counted twice.
- Pool is `source.file.readers` workers over a queue twice that size, so the feeder blocks on
  a full queue instead of claiming files it cannot yet read.
- `start()` refuses when the watcher could not set up its inotify watch; creates the done and
  failed directories first.
- Drained file to done, source failure to failed. `stop()` wakes the watcher, joins the
  feeder, drains the pool, so no file is left half processed.

### Ingestor Factory

`inc/ingest/ingestor_factory.hpp`, `src/ingest/ingestor_factory.cpp`.

Source mode to ingestor. One shared instance registers `file` and `rabbit` at construction.
`createIngestor()` builds a fresh one by name around the sink it is given, null when
unregistered; `hasIngestor()` answers without building. Name from `source.mode`, so `main`
never names an ingestor and a new mode is one `registerIngestor` call and a class behind
`IIngestor`.

### Rabbit Ingestor

`inc/ingest/rabbit_ingestor.hpp`, `src/ingest/rabbit_ingestor.cpp`.

- `source.rabbit.consumers` threads over one queue. Unknown `source.format` fails `start()`
  before a socket opens. Nothing else does: `start()` builds one `RabbitConn` per consumer
  and starts the threads, each connecting on its own, so a broker not up yet is not a startup
  failure.
- Each thread loops until the stop flag. On a connect failure it waits `kRabbitBackoffMinMs`,
  doubling per further failure up to `kRabbitBackoffMaxMs`. The wait is slept in
  `kRabbitBackoffSliceMs` slices, checking the stop flag between them, so a shutdown during a
  backoff waits out one slice and not the whole delay.
- Only the first consumer's first failure is logged warn, the rest at debug, so a broker down
  for a minute does not fill the log; the reconnect logs once.
- Connection up: build a `RabbitSource`, loop — batch to the sink, then one
  `ack(last_tag, true)` for the whole batch, one round trip per batch not per message.
- A failed read or ack breaks the inner loop; the thread reconnects and builds a fresh
  `RabbitSource`, since a new channel restarts the delivery tags. The unacked batch is
  redelivered.
- `stop()` sets the flag, joins every thread, drops the connections; each thread logs its
  record and reject counts.
- The ack goes after the sink wrote the batch, not before: the safe order for loss, and the
  reason a batch can be counted twice. If the connection dies between write and ack the
  broker redelivers and the aggregation adds it again. Delivery is at least once and the
  aggregator is not idempotent, so a reconnect can leave counters slightly high. The file
  path avoids this with the progress mark; the queue has no equivalent, its batches are
  consumed with an empty source name and nothing is marked.

### Dir Watcher

`inc/ingest/dir_watcher.hpp`, `src/ingest/dir_watcher.cpp`.

- The sender writes elsewhere and renames into the input directory, so a file that appears is
  already whole. Inotify watch for `IN_MOVED_TO`; `next_file` hands out one path at a time
  and blocks until something arrives. Not thread safe, wants its own thread.
- A claim is a rename from input to target. Atomic on the same filesystem, so two processes
  cannot both win a file, and a file being worked on is invisible to a later watcher. A
  failed rename logs a warning and leaves the file alone.
- Startup sweeps both directories: target first, already claimed and straight on the queue,
  then input, claimed as if just arrived. That picks work back up after a crash.
- A watch that cannot be set up leaves the watcher not `ok()`, and `next_file` returns false
  instead of blocking.
- The wait is `poll` over the inotify fd and an owned eventfd. `wake()` writes that eventfd,
  unblocking `next_file` and making it return false; the one thread-safe entry point, so
  another thread can end the wait to shut down.

## Delta

`inc/aggregate/delta.hpp`.

- Header only, plain structs. One batch in three maps: `subs` by subscriber MSISDN, `ops` by
  operator code, `links` by pair. Every counter starts at 0, so a first record adds into a
  fresh entry without a lookup.
- `SubDelta`: call seconds each way, bytes each way, messages each way, unanswered, busy,
  failed. `OpDelta`: voice and sms only. `LinkDelta`: seconds, calls, messages one pair
  exchanged.
- `LinkKey` is directed: owner to peer and peer to owner are two entries. `LinkHash` runs
  each half through a splitmix64 finalizer and combines with an offset, so swapping them
  changes the hash.
- A subscriber entry is 72 bytes of counters plus the map node, so millions of subscribers
  measure in hundreds of megabytes.

## Aggregator

`inc/aggregate/aggregator.hpp`, `src/aggregate/aggregator.cpp`.

- `fold()` walks a batch once into a `Delta`, clearing it first and reusing its buckets.
  Nothing kept between calls, so any number of threads can fold at once, each with its own
  `Delta`.
- Subscriber bucket keyed by MSISDN: calls add seconds each way, messages one each way, data
  its byte counts, unanswered, busy and failed each their own counter.
- Operator bucket keyed by the MCCMNC of the subscriber's own IMSI, voice and sms only; an
  IMSI too short counts nowhere.
- Calls and messages also add to the link between the two parties.

## Totals

`inc/aggregate/totals.hpp`, `src/aggregate/totals.cpp`.

- Fourteen counters, flat for the whole run. Named after the usage types, since the generator
  counts the same fourteen and has no subscribers to key them by. `data_dur` is counted
  though the aggregates never use it.
- `Totals` is a plain struct. `add()` counts one record or a batch before any check a `Delta`
  makes, so a record without a subscriber MSISDN still counts. `format()` renders the block
  as the tail of one log message: tab, padded name, value.
- `RunTotals` is the same counters as atomics: `merge()` folds a batch in with one relaxed
  `fetch_add` per non-zero counter, `snapshot()` reads them back. No record touches an atomic,
  and a snapshot taken mid merge can hold part of a batch.

## Aggregate Writer

`inc/aggregate/aggregate_writer.hpp`, `src/aggregate/aggregate_writer.cpp`.

- Folded `Delta` into store calls. Subscribers to `sub:<msisdn>`, operators to `op:<mccmnc>`,
  links to `link:<owner>` with fields `<peer>:dur`, `<peer>:cnt`, `<peer>:sms`, so a
  subscriber's peers are one hash and not one key per edge.
- Counters of 0 are skipped, the batch never touched them.
- Holds only the store, so threads can share one writer if the store allows. Key and field
  built into two reused buffers, so a batch costs no allocation past the first entry.
- Store flushed once at the end; the batch is reported failed if any counter or the flush
  failed.
- A second overload writes a batch's `Totals` to `total:proc`, fourteen fields at most. It
  queues and does not flush: it runs before the delta write, which drains both. The other way
  round the totals wait for the next batch and the last batch never goes out.
- The hash sums every run while the block logged at shutdown is one run, so a comparison
  starts with `redis-cli del total:proc`.

## Rank Writer

`inc/aggregate/rank_writer.hpp`, `src/aggregate/rank_writer.cpp`.

- The same folded `Delta` into board calls. A subscriber scores on `top:voice` from its two
  call durations, `top:sms` from its two message counts, `top:data` from its two byte
  counters, `top:fail` from no-answer, busy and failed together. An operator scores on
  `top:op-voice` and `top:op-sms`.
- Member is the msisdn or the mccmnc, so a board hands back the same id the lookup routes
  take. A score of 0 is skipped, like a counter of 0.
- Does not flush. Runs before the delta write, which drains what both queued, so a batch's
  hashes and boards commit in one transaction or not at all. Member buffer reused down the
  delta, no allocation past the first entry.
- Four `ZINCRBY` per subscriber and two per operator on top of nine and four `HINCRBY`, all
  pipelined and none read: about forty percent more commands for a ranking that would
  otherwise cost an `HGETALL` per key on every read.
- Two ranking questions a sorted set cannot answer: ratios, not incrementally maintainable,
  and peer count, which needs the `HINCRBY` reply the pipeline discards.

## Store

`inc/store/istore.hpp`.

- `increment()` adds a value to one field of one key, `rank()` a score to one member of one
  board, `flush()` completes everything queued.
- `resume_at()` reads how far one source was applied. `mark()` queues the new high-water mark
  without completing it, so the mark goes out with the increments it belongs with rather than
  ahead of them.
- All called from several threads at once, so an implementation owns its locking or gives each
  thread its own state.
- Keys, fields, numbers, a source name. Nothing about records or aggregates.

### Redis Conn

`inc/store/redis_conn.hpp`, `src/store/redis_conn.cpp`.

- One context per thread, opened on first use, freed when the thread ends. `get()` opens it,
  and opens a new one when the old broke; `peek()` returns without opening; `drop()` frees.
- Host, port and timeout from `config.toml`; the timeout covers the connect and every command
  after. Knows nothing of pipelines or commands, so the read side reuses it as is.
- A failed connect backs that thread off rather than retrying per call: first failure waits
  `kRedisBackoffMinMs`, each further doubles up to `kRedisBackoffMaxMs`, `get()` returns null
  until the wait is out. Nothing sleeps, the caller is refused and comes back later, so a
  store that is down costs one failed connect per backoff and not one per counter.
- First failure logged error, the rest debug until a connect succeeds, which logs once and
  clears the backoff.

### Redis Store

`inc/store/redis_store.hpp`, `src/store/redis_store.cpp`.

- One hash per key, every increment an `HINCRBY` appended to a hiredis pipeline on this
  thread's `RedisConn` context, so no lock on the write path. `rank()` appends a `ZINCRBY`
  into the same pipeline and open batch, so a board is closed by the flush that closes the
  counters beside it. Pipeline depth is the store's own, one counter per thread.
- Commands go out without waiting for replies. The pipeline drains at
  `kRedisPipelineDepth` commands, and `flush()` drains the rest, one reply per command.
- A reply carrying an error is counted a failure and logged, so a rejected increment is not
  read as a write. A broken connection is dropped and its queued commands lost, counted and
  logged; the next call opens a new one, or is refused while backing off. Either way the
  write returns false and the batch is reported not fully written.
- Everything between two flushes is one transaction. `MULTI` is queued lazily by the first
  command after a flush and `flush()` closes it with `EXEC`, so nothing above the store opens
  or commits anything. The depth drain reads the `+QUEUED` replies the server answers inside a
  transaction with, so a batch larger than the pipeline still goes out in one commit. Redis
  truncates a trailing partial `MULTI` when it loads the AOF, so a batch killed halfway lands
  whole or not at all.
- `resume_at()` is one `HGET` of the source's field of `prog:file`, a missing field reading 0.
  It flushes first, so no reply of this thread's sits ahead of the one it waits for.
- `mark()` is one `HSET` into the open batch, which makes the mark and the counters it
  describes commit together: written before them, a crash between the two would lose records
  for good.
- Cost per increment is the append into the output buffer; the round trip is paid once per
  1024 commands.

### Store Factory

`inc/store/store_factory.hpp`, `src/store/store_factory.cpp`.

Store type to store. One shared instance registers at construction, one line per backend.
`createStore()` builds a fresh one by name, null when unregistered; `hasStore()` answers
without building. Name from `store.type`, so a new backend is one `registerStore` call and a
class behind `IStore`.

## Sink

`inc/sink/isink.hpp`.

- `consume()` takes ownership of a batch and the name of its source, empty when that source
  cannot be resumed.
- `resume_at()` answers how far a source was consumed, 0 by default, so a sink that keeps no
  progress needs nothing of it.
- The ingestor's workers call it from several threads at once, so an implementation owns its
  own locking.

### Aggregate Sink

`inc/sink/aggregate_sink.hpp`, `src/sink/aggregate_sink.cpp`.

- Holds an `Aggregator`, the `IStore`, and an `AggregateWriter` and `RankWriter` over it, so
  it names no backend; a null store is refused at construction.
- `consume()` folds the batch into a `Delta` and a `Totals`, merges the totals into
  `RunTotals`, then writes the totals, the boards and the counters in that order.
- `snapshot()` hands the run's counters back, records that reached nothing included.
  `resume_at()` is the store's answer passed through.
- A batch with a source name also has its highest sequence marked, queued between the totals
  and the board write, so the mark rides the flush the delta write ends with and commits in
  the same transaction as the counters. No source name, or nothing in the batch: nothing
  marked. Only the delta write flushes, so totals, mark, boards and counters are one commit.
- The `Delta` is one per thread and lives past the call, so buckets are reused and a steady
  stream allocates nothing. The `Totals` is one per call, on the stack.
- Nothing is locked: the fold reads only the batch, the merge is fourteen relaxed atomic adds,
  the store gives each thread its own connection. A batch that did not fully land is logged by
  the writer and dropped.

## Query Store

`inc/query/iquery_store.hpp`.

- Read side of the same counters: `hgetall()` every field of a key, `hkeys()` the field names,
  `hmget()` the fields it is named, `dbsize()` the key count, `top()` one page of a board with
  the board's cardinality beside it.
- Each clears its output first and returns false only when the store could not be reached, so
  a key that does not exist is a success with nothing in it.
- `Ranked` sits beside `Fields` in the same shape, a vector of pairs owned by the caller.
- Called from several threads at once. Keys, fields, members, numbers.

## Redis Query

`inc/query/redis_query.hpp`, `src/query/redis_query.cpp`.

- `HGETALL`, `HKEYS`, `HMGET`, `DBSIZE` over this thread's `RedisConn` context, one round trip
  per call, no lock, no state.
- Keys and field names go out as lengths and bytes, so a `string_view` over a larger buffer is
  read as it stands. Values come back as strings; a non-string element reads as empty.
- `top()` costs two round trips: `ZCARD` for the cardinality, then
  `ZREVRANGE start stop WITHSCORES` for the page, `-1` as stop when no limit was asked for. A
  score arrives as text because Redis keeps it as a double and prints it with `%.17g`, so it
  is read with `strtod` and truncated. That caps an exact score at 2^53, which no counter here
  reaches.
- A broken connection is logged and dropped, the next call opens a new one, the read returns
  false. A reply carrying an error is logged and returns false too, so a rejected read is
  never read as an empty key. `hmget()` with no field names asks nothing and succeeds.
- Cost per call: one round trip, one allocation for the reply, one per value kept.

## Result

`inc/query/result.hpp`.

The `{ int status; std::string body; }` every service hands back. Four lines of its own so the
four services and the gateway name one type rather than one service's nested one.

## Query Params

`inc/query/query_params.hpp`, `src/query/query_params.cpp`.

- Four optional parameters a listing route takes: `weights`, `sort`, `offset`, `limit`.
- `parseParams()` fills it from an `httplib::Request` and returns a `Result`: 200 with empty
  body when parsed, 400 naming the parameter when not.
- `weights` only `0` or `1`, `sort` only `dur` or `sms`, `offset` and `limit` a whole number
  end to end, so `10x` is refused rather than read as 10.
- No limit named gets the route's fallback; over the cap is clamped and reported clamped. A
  limit of 0 asks for every entry, from the query string or a caller filling the struct.
- `page()` cuts a vector to offset and limit, empty when the offset is past the end.
- Both live here rather than in a handler so the rule is written once and can be tested
  without a running server. The board name is not this file's business: it arrives as a path
  element and `RankService` knows which boards exist.

## Links

`inc/query/links.hpp`, `src/query/links.cpp`.

- `link_peers()` is `HKEYS` on `link:<msisdn>` with the metric suffix dropped, sorted and
  deduplicated: the cheap read the path search wants.
- `link_weights()` is `HGETALL` on the same key with a peer's three fields folded into one
  `Peer`: a hub costs one read of every field, the price of ranking its peers correctly
  rather than returning an arbitrary slice.
- `order_peers()` sorts by either metric descending, ties by the other metric then by msisdn,
  so paging is stable.
- Both take the store as an argument and keep nothing, so `QueryService` and `PathService`
  read the link hash through one place instead of each halving field names for itself.

## Query Service

`inc/query/services/query_service.hpp`, `src/query/services/query_service.cpp`.

- `msisdn()` and `op()` read one hash whole and rename its fields to the response names; byte
  counters go out as bytes, the unit they are stored in.
- `link()` reads the three fields of one pair; a pair written before the call count was kept
  reports zero calls rather than a missing field.
- A failed read is 503, a key with no fields 404.
- `peers()` takes parameters. Without `weights`: `link_peers()` paged, order still msisdn
  ascending, plus the count of every peer whether returned or not. With `weights`:
  `link_weights()` ordered by the chosen metric and paged, each peer carrying duration, calls
  and message count. Both report `count`, `offset` and `limit`, so a caller can page without a
  second call.
- Nothing kept between calls but the store reference, so one instance serves every handler
  thread the store is safe for.

## Path Service

`inc/query/services/path_service.hpp`, `src/query/services/path_service.cpp`.

- `path()` runs a breadth first search from both parties at once, expanding the narrower
  frontier each round so the search stays off the hubs, and joins the trails at the shared
  subscriber.
- One store read per subscriber expanded. Gives up after `query.max_hops` rounds or
  `query.max_visited` subscribers. The 404 body carries both bounds, so a caller states the
  limit rather than guessing it.
- With `weights` it resolves the path it holds: one `HMGET` per hop after the search, bounded
  by the hop limit. A hop that reads empty reports zeros rather than failing, since the link
  exists and the search walked it. Here it is one pass; from a caller it is one round trip per
  hop.

## Stats Service

`inc/query/services/stats_service.hpp`, `src/query/services/stats_service.cpp`.

- `health()` is one `dbsize()`, which proves the connection and returns the key count in the
  same round trip. Always 200: the route reports on the gateway, and the gateway answered, so
  an unreachable store is `"store":"down"` with `"keys":0` rather than 503. Path bounds go out
  with it, read from config.
- `totals()` is one `HGETALL` on `total:proc`, the fourteen fields under the stored names with
  underscores turned into hyphens. A missing or empty hash is 200 with every field 0, not 404:
  a store that has processed nothing is a real state, and unlike a subscriber the store itself
  always exists. Only an unreachable store is 503.
- Counters are lifetime, every run since the hash was last cleared; nothing invents a run
  identity.

## Rank Service

`inc/query/services/rank_service.hpp`, `src/query/services/rank_service.cpp`.

- `top()` maps a board name to its key, one of `voice`, `sms`, `data`, `fail`, `op-voice`,
  `op-sms`, refuses anything else with 400, hands offset and limit to `IQueryStore::top()`.
- The store pages, so cost is `O(log n + k)` and no key is scanned.
- Response carries the board, its whole cardinality, the offset and limit served under, and
  the page as `{ id, score }` objects. An empty board is 200 with no entries, never 404.

## Query Factory

`inc/query/query_factory.hpp`, `src/query/query_factory.cpp`.

Store type to the read side of that store. One shared instance registers at construction, one
line per backend. `createQuery()` builds a fresh one by name, null when unregistered;
`hasQuery()` answers without building. Same `store.type` the writers use, so both sides of one
backend are registered together and the gateway names no class of its own.

## Http Gateway

`inc/query/http_gateway.hpp`, `src/query/http_gateway.cpp`.

- HTTP front of the query API, cpp-httplib behind it. Takes the `IQueryStore` and holds the
  four services by value, so nothing above it constructs or names them; they are stateless and
  cost a reference each.
- The constructor builds the server and calls `registerQueryRoutes()`, `registerStatsRoutes()`
  and `registerRankRoutes()`, which bind eight routes, each handing its captures to a service
  call and sending the result as `application/json` under the status it came with.
- Three run `parseParams()` first and send its 400 unchanged, so a bad parameter never reaches
  a service.
- A path matching no route answers 404 with a JSON body, and a handler that throws is logged
  and answered 500, so a failing query never takes the listener down. Every answered request
  logs method, path and status, unknown routes included.

| Route | Answers |
|---|---|
| `GET /query/msisdn/{n}` | nine counters, bytes as bytes |
| `GET /query/operator/{mccmnc}` | four counters |
| `GET /query/link/{n}` | peers, optionally weighted, sorted and paged |
| `GET /query/link/{a}/{b}` | duration, calls and sms |
| `GET /query/path/{a}/{b}` | path, optionally per-hop weights |
| `GET /query/health` | gateway and store state, key count, path bounds |
| `GET /query/totals` | the store's fourteen lifetime counters |
| `GET /query/top/{board}` | one page of a ranking, six boards |

200 with a body, 400 on a bad parameter, 404 for an entity never seen, 503 when the store is
unreachable, 500 when a handler throws, JSON on every one. `/query/health` never 503s;
`/query/totals` and `/query/top` never 404.

- Port and host are constructor arguments, taken by `gateway_main` from `query.port` and
  `query.host`. `start()` binds and serves on its own thread, returning as soon as the port is
  taken and false when it is not. Port 0 binds any free one and `port()` reports which.
- `stop()` ends the listener and joins that thread; the destructor calls it, so a gateway that
  goes out of scope leaves nothing running.
- Served by a thread pool of `query.concurrency` threads, one request at a time each, and the
  store gives every thread its own connection, so nothing is locked between handlers. The
  response body is moved into the response.

## Config

`inc/config.hpp`, `src/config.cpp`.

- Singleton, built on first use. `load()` reads `config.toml` with toml++, `validate()` checks
  the values. Both private, so nothing can change the config later.
- `query.max_hops` and `query.max_visited` live here rather than in `constants.hpp` because
  they are the two numbers worth tuning per deployment and the API states them on screen.
- The header defines `inline const Config& cfg`, so any file including it reads settings as
  `cfg.rabbit.url`.
- Bad values throw and stop the program at startup.

## Constants

`inc/constants.hpp`.

Header only, one `inline constexpr` per name, no namespace. Four kinds:

- Sizes: `kBatchSize` is 4096 records, taken by both `kFileBatchSize` and `kRabbitBatchSize`,
  so a file batch and a queue batch cost the store the same. `kRabbitPrefetch` is two batches,
  so a consumer always has the next batch in hand while it writes the current one; a static
  assert keeps it inside the `uint16_t` the AMQP field is. `kRedisPipelineDepth` is 1024
  commands between drains.
- Waits: `kRedisBackoffMinMs` to `kRedisBackoffMaxMs` for a store that is down,
  `kRabbitBackoffMinMs` to `kRabbitBackoffMaxMs` for a broker that is, and
  `kRabbitBackoffSliceMs` for how long a backoff may delay a stop.
- Names: `sub:`, `op:`, `link:`, the six `top:` boards, `total:proc`, `prog:file`, the counter
  fields and field suffixes, and the names those counters go out under in JSON. The writer and
  the reader take the same constant, which keeps a rename from turning into a key nothing
  reads.
- The rest: the record field count the parser splits on, the default and maximum page sizes
  the listing routes clamp to, and `kMsinDivisor`, the power of ten that strips the MSIN off an
  IMSI and leaves the MCCMNC.

## Logger

`inc/logger.hpp`, `src/logger.cpp`.

Singleton. `Debug < Info < Warning < Error < None`. Below the active level is dropped, `None`
drops everything. Level from `cfg.log.level`.

```
MM-DD HH:MM:SS [LEVEL] [Component] -> message
```

- Time gray, level tag colored: gray debug, green info, yellow warn, red error.
- Component is the name the caller passed, one constant per translation unit, which is what
  makes a line traceable to the class that wrote it.
- Write with `logDebug`, `logInfo`, `logWarn`, `logError`. Output to stderr, a mutex keeps
  lines from mixing when threads log at once.

## Thread Pool

`inc/util/thread_pool.hpp`, `src/util/thread_pool.cpp`.

- Fixed workers, bounded queue. Worker count and queue size both above 0 or the constructor
  throws.
- `submit()` takes a `std::function<void()>`, pushes it, wakes one worker. On a full queue it
  blocks until a worker frees a slot, so a fast producer cannot outrun the workers and grow
  memory without bound. False only when the pool is already stopping, true once queued.
- One mutex guards the queue. Two condition variables on it: `m_cv_full` for producers waiting
  for a slot, `m_cv_empty` for workers waiting for a task. A worker waits, pops under the lock,
  drops the lock, runs the task outside it, so workers do not block each other.
- An exception out of a task is caught in the worker loop and logged; the worker takes the next
  task and never dies.
- The destructor sets the stop flag, wakes everyone, joins. Workers leave the loop only once
  the queue is empty, so queued work is drained, not dropped. The worker vector is declared
  last so threads start after the rest of the state is live.
- Not copyable. Safe to submit from many threads and from inside a running task.
- Tests in `tests/util/thread_pool.cpp`: public API only, every wait bounded, so a broken pool
  fails instead of hanging the suite.

## Mapped File

`inc/util/mapped_file.hpp`, `src/util/mapped_file.cpp`.

- One read only `mmap`, so a 5 GB file costs no copy and no heap. The constructor opens the
  path, checks it is a regular file, maps it, and tells the kernel to expect a sequential read.
  The destructor unmaps.
- `ok()` says whether the mapping worked, `data()` and `size()` hand the bytes over as they
  are. An empty file is `ok()` too, null pointer and size 0. Every failure is logged with the
  path and leaves `ok()` false, so the caller checks once.
- The fd is closed right after the map, since the mapping keeps its own reference. Not
  copyable: two owners would unmap the same pages twice.

## Json

`inc/util/json.hpp`, `src/util/json.cpp`.

- Builds the JSON object for the query responses. `add()` takes a string, a number, a vector of
  strings or a vector of `Json`, appending the field to one buffer, so fields come out in the
  order added and `str()` only wraps that buffer in braces.
- The vector of `Json` builds the weighted peers, the path hops and the board entries: each
  element is a whole object, so one nesting level costs no parser and no tree. `error()` builds
  the one field object failing queries answer with.
- Every name and string value goes through `quoted()`, escaping quotes, backslashes and control
  characters, so text that came in over HTTP cannot break the body. One pass per string; other
  bytes copied as they are.
- Nothing is parsed or validated, so a caller adding the same name twice writes it twice.

## Fs

`inc/util/fs.hpp`, `src/util/fs.cpp`.

- Two free functions, no class, no state.
- `ensure_dir()` creates a directory and the parents it needs and returns whether it exists
  afterwards, so a path already there is a success. A failure is logged with the path and
  reported, never thrown: the callers are the watcher and the ingestor, which answer for a
  missing directory by refusing to start.
- `basename_of()` cuts a path back to the text after the last `/`, or hands back the path when
  it holds none. That is what names a source: the ingestor keys the sink's progress by a file's
  own name and not the directory it sits in, so a file that moves between ready, processing and
  done is the same source throughout.

## Signal Waiter

`inc/util/signal_waiter.hpp`, `src/util/signal_waiter.cpp`.

- `SIGINT` and `SIGTERM` blocked in the constructor with `pthread_sigmask`, before either main
  starts anything. A thread inherits the mask of the thread that started it, so every worker
  started later is born with both blocked and neither can be delivered to it. That leaves the
  signal pending for `wait()`, a `sigwait` on the same mask, retried when the wait is
  interrupted.
- `wait()` returns on the main thread and outside any handler, so the shutdown that follows can
  log, take locks, join threads and flush the store, none of which is safe inside a handler.
- The destructor restores the previous mask, which lets a test construct one and leave the
  process as it found it.
- Both mains hold one for the run: the processor waits, stops the ingestor, logs the run's
  totals; the gateway waits, logs the signal number, stops the listener.

## UI Backend

`ui/api/`. FastAPI, uvicorn, httpx, sqlite3. Modules imported by plain name, so the process
runs with `ui/api` on the path: `uvicorn main:app --app-dir ui/api`, or `python api/main.py` in
the container.

### Settings

`ui/api/settings.py`.

- Reads `config.toml` once at startup with `tomllib`: `[ui]` for `gateway_host`, `api_port`,
  `sample_interval`; `[query] port` for the gateway port; `[redis]` for the address the system
  screen reports. The file is the one `CDR_CONFIG` names, else the nearest walking up from the
  working directory.
- `[query] host` is a bind address and deliberately not reused as a client address, which is
  why `[ui] gateway_host` exists. The container runs on the host network, so that address is
  `127.0.0.1` in docker and out of it, and `api_port` is the port bound rather than mapped.
- Everything else the backend runs on is in `ui/api/constants.py`; everything tuned per view is
  in the browser.

### Gateway Client

`ui/api/gateway.py`.

One `httpx.AsyncClient` against `http://{gateway_host}:{query.port}`. `get()` never raises: a
connection failure is 502, an overrun timeout 504, any status the gateway sent passes through
with its body. Each call records how that route last answered, which is what the system screen
lists. The path route gets the longer timeout, the only one that needs it.

### Samples

`ui/api/db.py`.

- One SQLite table, `ts` primary key with the key count and the fourteen counters as columns. A
  connection per call: a row every few seconds and a few hundred rows a read is nothing worth
  pooling.
- `append()` writes what the gateway sent and zero for what it did not. `sweep()` drops rows
  past `RETENTION_DAYS`. `stats()` reports the file for the system screen.
- `series()` derives what a chart reads. A metric is one of the fourteen, the key count, a sum
  of counters (`calls`, `messages`, `data-vol`, `failures`), or a ratio (`fail-share`, the three
  averages). A `:rate` suffix asks for `(value - previous) / seconds` between samples instead of
  the counter; a ratio has no rate and is refused.
- Counters only rise, so a negative delta means the store was flushed and clamps to zero: the
  chart carries on flat rather than spiking, at the cost of a reset looking like a quiet minute.
- The window reads one row before it so the first rate has a baseline, and the result is thinned
  to `MAX_POINTS` by keeping the last point of each bucket.

### Sampler

`ui/api/sampler.py`.

An asyncio task started with the app. Each tick calls `/query/health` and `/query/totals`
together and appends one row. A failed call, or a store reporting down, writes nothing and is
counted: a gap in the data is the truth about that minute. It keeps the cadence when nothing
moved rather than skipping the row, so a flat line means idle and only a gap means failure.
Retention is swept once a day off the same tick. SQLite writes go through `asyncio.to_thread`,
so the event loop never blocks on the file.

### Config Document

`ui/api/config_file.py`.

Reads `config.toml` twice over: `tomllib` for the values, a line scan for the shape the config
screen shows, sections in file order, the comment block above each as its help, the trailing
comment on each key. Banner rules reset the block, so a section carries its own prose and not
the file header's. `source.mode`, `source.format` and `store.type` say which sections are live;
the rest are marked inactive with the setting that turned them off. It reports the file, not any
running process.

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

- `lib/api.ts` is the only place a URL is written, and turns a failing response into an
  `ApiError` carrying the status. Screens render on that status: 502 the gateway is down, 503
  the store is, 504 a search timed out, 404 never seen. The three failures never render alike.
- `lib/format.ts` is the only place seconds, bytes, counts and shares are formatted.
- `lib/settings.ts` holds the browser's settings, theme, refresh, default window, peer page,
  board page, expand limit, canvas cap and edge metric, in local storage behind
  `useSyncExternalStore`. `lib/history.ts` holds recent lookups the same way.
- Routing is the URL hash, so a screen is a link and the static mount needs no fallback.
- The graph screen keeps nodes and edges in a ref and steps a repulsion, spring and centring
  simulation on `requestAnimationFrame`. A node's size is the sum of its **known** edges, the
  ones fetched rather than all it has, which the legend says out loud. An expansion pulls
  `expandLimit` peers heaviest first and says "showing 50 of 812" when there are more; at the
  canvas cap an expansion is refused with the reason instead of freezing.
