# Query API Changes

What the web UI needs from the processor and the gateway. One fix, three new routes, two
routes that grow optional parameters, one addition to the write path, two constants that move
into `config.toml`.

Still one binary each side. `build/gateway` links every source but the other main, so the new
files need no Makefile change, and the new classes are constructed in `gateway_main` beside
the one that is there now.

This is a working document. Once the changes are in, the descriptions fold into
`HL_Design.md` and `DL_Design.md` and this file goes away.

## Rules

- Minimal. Only what is wrong, and only what the UI cannot work without.
- Every route stays under `/query`.
- The five routes that exist keep the response they have today. Everything added is an
  optional query parameter, so nothing that reads them now has to change.
- No file grows past what it is about, and no class takes on a second job. Three new classes,
  one new util, one method added to the write interface and two to the read one.
- No CORS, no auth, no rate limiting. The browser never calls the gateway. The Python backend
  does, server side, and it is the only client.

---

## 1. Fix: byte units

`QueryService::msisdn` divides `data_rx` and `data_tx` by `kBytesPerKb` and reports KB, while
the same counters sit in the store as bytes. One quantity, two units, depending on the route
it came out of.

Report bytes everywhere. Drop the division, drop `is_bytes()`, drop `kBytesPerKb`. Field
names stay `data-out` and `data-in`, so the change is silent to a reader that expected KB.
Acceptable: the only reader is the UI, and it formats bytes itself.

---

## 2. New: `GET /query/health`

Is the gateway answering, and can it reach the store.

```json
{ "status": "ok", "store": "up", "keys": 214893, "max-hops": 6, "max-visited": 10000 }
```

- The store check is one `DBSIZE` per call. It proves the connection and returns the key
  count in the same round trip, so the check costs nothing extra.
- `keys` is every key in the database together: subscribers, operators, links, the boards and
  `total:proc`. A scale number, not a subscriber count, and the UI labels it as one.
- A store that does not answer gives `"store": "down"` and `"keys": 0`. The status stays 200.
  The route reports on the gateway, and the gateway answered.
- `max-hops` and `max-visited` are the path bounds from config (§7), so the UI never states a
  limit it guessed.

---

## 3. New: `GET /query/totals`

The `total:proc` hash, the fourteen counters.

```json
{ "records": 0, "moc-cnt": 0, "mtc-cnt": 0, "sms-mo-cnt": 0, "sms-mt-cnt": 0,
  "data-cnt": 0, "noans-cnt": 0, "busy-cnt": 0, "failed-cnt": 0,
  "moc-dur": 0, "mtc-dur": 0, "data-dur": 0, "data-rx": 0, "data-tx": 0 }
```

These are lifetime counters, not this run's. `RunTotals` lives in the processor's memory and
is logged when the process ends; `total:proc` is incremented by every run since the store was
created and is never reset. The route reports what is in the store, and the UI says so on
screen. No run identity is invented anywhere.

- Names are the stored field names with underscores turned into hyphens, matching how the
  other routes name theirs.
- Byte counters go out raw.
- A missing or empty hash is 200 with every field 0, not 404. A store that has processed
  nothing is a real state the dashboard has to draw. The other routes 404 an entity that was
  never seen; this one describes the store itself, which always exists.
- 503 when the store is unreachable.

One `hgetall` on `total:proc`.

---

## 4. New: `GET /query/top/{board}`

Ranking. Without it nothing can find an interesting subscriber, only describe one that was
named.

```
GET /query/top/voice?limit=20&offset=0
```

```json
{ "board": "voice", "count": 98213, "offset": 0, "limit": 20,
  "entries": [ { "id": "972500000001", "score": 184920 } ] }
```

| Board | Key | Ranks | Score |
|---|---|---|---|
| `voice` | `top:voice` | subscribers | call seconds, out and in together |
| `sms` | `top:sms` | subscribers | messages, out and in together |
| `data` | `top:data` | subscribers | bytes, rx and tx together |
| `fail` | `top:fail` | subscribers | no-answer, busy and failed together |
| `op-voice` | `top:op-voice` | operators | call seconds |
| `op-sms` | `top:op-sms` | operators | messages |

The keys join the aggregate key prefixes in `constants.hpp`, beside `kSubPrefix`, `kOpPrefix`
and `kLinkPrefix`, with the board names the route accepts mapped to them by `RankService`.

- `count` is the board's cardinality, one `ZCARD`.
- `limit` defaults to 20, clamped to 500. An unknown board is 400.
- Descending by score, ties by id, so paging is stable.
- One `ZREVRANGE ... WITHSCORES`, O(log n + k). No scan, no keyspace walk.

### Why not a scan

`SCAN` itself is cheap — cursor-paged, a full sweep of ~200k keys at `COUNT 1000` is about
200 round trips. The cost is what the cursor forces: ranking subscribers means an `HGETALL`
per `sub:` key, so 100k reads per refresh. Redis is single-threaded, so that is time the
processor's `HINCRBY` pipeline is not being served — a leaderboard refresh becomes an ingest
stall, and it gets linearly worse as the keyspace grows. The result is a snapshot that is
stale on arrival.

A sorted set moves that cost to the write path, where it is O(log n) per update and paid once
per batch instead of once per refresh.

### What cannot be a board

Two kinds of ranking a `ZINCRBY` cannot express:

- **Ratios.** Failure rate, in-against-out asymmetry, average call length. A sorted set
  accumulates a sum; a division is not incrementally maintainable. `fail` ranks the absolute
  count, which is not the same question. Ranking by ratio would need a scan.
- **Peer count.** Maintaining it means knowing whether a link field was new, which is the
  `HINCRBY` reply — and `RedisStore::drain()` discards every reply, by design, because the
  writes are pipelined inside `MULTI`. Peer count stays O(1) for a named subscriber, as the
  `count` field of the peers route, and is not rankable.

---

## 5. Change: `GET /query/link/{msisdn}`

Today it returns the peer names, all of them, unordered and unweighted. A hub has thousands,
and the graph needs the weight of every edge it draws. Without this the UI issues one request
per peer.

Four optional parameters:

| Param | Default | Meaning |
|---|---|---|
| `weights` | `0` | `1` returns duration and sms per peer |
| `sort` | `dur` | `dur` or `sms`, the metric peers are ordered by |
| `limit` | `100` | peers returned, clamped to 1000 |
| `offset` | `0` | peers skipped |

Without `weights`, the response is what it is today plus the paging fields:

```json
{ "msisdn": "972500000001", "count": 812, "offset": 0, "limit": 100,
  "peers": ["972500004242", "972500009999"] }
```

With `weights=1`:

```json
{ "msisdn": "972500000001", "count": 812, "offset": 0, "limit": 100, "sort": "dur",
  "peers": [ { "msisdn": "972500004242", "duration": 930, "sms": 4 } ] }
```

- `count` is every peer the subscriber has, whether or not it was returned.
- Ordered by the sort metric descending, ties broken by the other metric, then by msisdn, so
  paging is stable.
- Without weights the order stays msisdn ascending, which is what `neighbours()` already
  produces.
- An unknown `sort`, or a non-numeric `limit` or `offset`, is 400. No silent fallback.
- A `limit` over the cap is clamped, and the response reports the clamped value.

`neighbours()` reads `hkeys` and halves the field names back into peers. The weighted path
reads `hgetall` on the same key instead and folds a peer's `:dur` and `:sms` fields together.
No new store method, and a hub is one read of every field: correct ranking beats a cheaper
read that can only return an arbitrary slice.

---

## 6. Change: `GET /query/path/{a}/{b}`

One optional parameter, `weights=1`, which adds what each hop carried:

```json
{ "path": ["972500000001", "972500004242", "972500009999"],
  "hops": [ { "from": "972500000001", "to": "972500004242", "duration": 930, "sms": 4 },
            { "from": "972500004242", "to": "972500009999", "duration": 60,  "sms": 0 } ] }
```

- One `hmget` per hop after the search finishes, bounded by `max_hops`.
- The gateway already holds the path. Resolving the hops here is one pass; from Python it is
  N round trips for a path the caller already has.
- A hop that reads empty reports zeros rather than failing. The link exists, the search walked
  it.

The not-found body carries the bounds it gave up at:

```json
{ "error": "path not found", "max-hops": 6, "max-visited": 10000 }
```

---

## 7. Config: path limits

`kMaxHops` and `kMaxVisited` are compile-time constants. The UI states them on screen when a
path is not found, and they are the two numbers worth tuning per deployment.

```toml
[query]
port        = 8080      # http port the query api listens on
host        = "0.0.0.0" # address the gateway binds
concurrency = 4         # handler threads, 0 for max
max_hops    = 6         # hops a path search covers before it gives up
max_visited = 10000     # subscribers a path search reads before it gives up
```

Validated on load like everything else: both above zero. Remove the two constants, and
`kBytesPerKb` with them, unused after §1. The section's prose gains a sentence about what the
two bounds cost when raised.

---

## 8. Interfaces

One method each. Nothing else moves.

`IStore`, the write side:

```cpp
/**
 * Adds value to one member of one board, creating either if missing.
 *
 * @param board: the key holding the members
 * @param member: the member within the board
 * @param value: the amount to add
 * @return false when the write could not be queued
 */
virtual bool rank(std::string_view board, std::string_view member, uint64_t value) = 0;
```

`RedisStore` implements it as `ZINCRBY`, queued through the same `batch()` and closed by the
same `flush()`, so a batch's counters and its board updates land in one transaction or not at
all — the same guarantee the hashes already have.

`IQueryStore`, the read side:

```cpp
/* The number of keys the store holds, false when it could not be read */
virtual bool dbsize(uint64_t& out) const = 0;

/* A board's members with their scores, highest first */
using Ranked = std::vector<std::pair<std::string, uint64_t>>;

/* One page of a board, highest score first, with the board's cardinality */
virtual bool top(std::string_view board, std::size_t offset, std::size_t limit,
                 Ranked& out, uint64_t& count) const = 0;
```

`Ranked` sits beside `Fields` in the same interface, in the same shape: a vector of pairs,
owned by the caller, cleared on entry.

`RedisQuery` implements them as `DBSIZE` and as `ZCARD` plus `ZREVRANGE ... WITHSCORES`.
`/query/totals` needs neither: it is an `hgetall` the interface already has.

---

## 9. Where the code goes

The point of this section is that nothing already written takes on a second job.

### RankWriter — new

`inc/aggregate/rank_writer.hpp`, `src/aggregate/rank_writer.cpp`.

Takes an `IStore&` and writes the boards a `Delta` implies, the way `AggregateWriter` writes
the hashes a `Delta` implies. Same shape, same skip-zero rule, same store. `AggregateWriter`
is not touched: it is about hashes, and boards are a different thing written from the same
input.

`AggregateSink` holds both writers and calls both per batch, then one `flush()` covers the
lot.

Cost on the write path: four `ZINCRBY` per subscriber per batch on top of nine `HINCRBY`, and
two more per operator. Roughly forty percent more commands, all pipelined, none of them
reading a reply. Boards are ~40MB of Redis for 100k subscribers.

### StatsService — new

`inc/query/stats_service.hpp`, `src/query/stats_service.cpp`.

Answers `health()` and `totals()`. Takes the same `IQueryStore` and hands back the same
`{ status, body }`. `QueryService` is entity lookups and its doc comment says so; reporting on
the store is a different job and gets a different class.

### RankService — new

`inc/query/rank_service.hpp`, `src/query/rank_service.cpp`.

Answers `top(board, params)`. Maps a board name to its key, refuses an unknown one, pages.
Ranking is a listing, not a lookup and not a report, so it is neither of the other two.

### Result — moved

The shared `{ status, body }` struct moves out of `QueryService` into `inc/query/result.hpp`,
so all three services and the gateway name the same type. Four lines and a doc comment.

### QueryParams — new

`inc/query/query_params.hpp`, `src/query/query_params.cpp`.

A struct of the parsed parameters and one function that fills it from an `httplib::Request`,
clamping `limit` and rejecting a bad `sort`, a bad `weights` or a non-numeric `limit` or
`offset`. Clamping and the 400 rule live in one place instead of being repeated across three
handlers, and they are testable without a running server.

The board name is not its business: it arrives as a path element, not a query parameter, and
`RankService` refuses an unknown one because it is the class that knows which boards exist.

### QueryService — grows, does not spread

`peers()` takes the parsed parameters. Folding `:dur` and `:sms` into peers, sorting them,
slicing the page, and resolving path hops go in private statics beside `walk()` and
`neighbours()`, which are already exactly that kind of helper. No new file: this is the
class's own work.

### HttpGateway — one file, three-line constructor

Route registration moves into private methods, `registerQueryRoutes()`,
`registerRankRoutes()` and `registerStatsRoutes()`, each holding its own lambdas. The
constructor keeps the server setup and calls them.

Not split across two `.cpp` files: every class in this tree is one header and one source, and
one class over two sources would be the only exception in it.

### Not touched

`processor_main`, the ingest path, the parser, the aggregator, `AggregateWriter`, the key
schema and the stored field names. The write path gains a second writer beside the first; it
does not change the first.

---

## 10. Left open for detection

Anomaly detection is **not** in this scope. Nothing here is built for it. What follows is the
check that nothing here forecloses it either:

- The boards answer "who is extreme at X" without a scan, which is the hard half of finding a
  candidate.
- The per-subscriber counters and the peer `count` answer the rest — fan-out with short calls,
  sms out against sms in, failed against total — for any subscriber a board hands over.
- A rule evaluator would be a fourth service beside `QueryService`, `StatsService` and
  `RankService`, over the same `IQueryStore`, needing no new store method.
- What it would still need, and what is deliberately absent today: somewhere to keep fired
  alerts, and a time dimension for anything phrased as a burst rather than a shape. Lifetime
  counters can say "this number calls 400 people and answers nobody". They cannot say "this
  started an hour ago".

---

## 11. Route summary

| Route | State | Answers |
|---|---|---|
| `GET /query/msisdn/{n}` | fixed | nine counters, bytes not KB |
| `GET /query/operator/{mccmnc}` | unchanged | four counters |
| `GET /query/link/{n}` | grown | peers, optionally weighted, sorted, paged |
| `GET /query/link/{a}/{b}` | unchanged | duration and sms |
| `GET /query/path/{a}/{b}` | grown | path, optionally per-hop weights |
| `GET /query/health` | new | gateway and store state, key count, path bounds |
| `GET /query/totals` | new | the store's fourteen lifetime counters |
| `GET /query/top/{board}` | new | one page of a ranking, six boards |

Status codes stay as they are: 200 with a body, 400 on a bad parameter, 404 for an entity
never seen, 503 when the store is unreachable, 500 when a handler throws, JSON on every one.
`/query/health` never 503s, and `/query/totals` and `/query/top` never 404.

---

## 12. Tests

Doctest, beside the existing tests, against the fakes they already use. The fake query store
gains `dbsize()` and `top()`; the fake write store gains `rank()`; both gain a way to fail.

- `tests/aggregate/rank_writer.cpp` — new. Each board from one `Delta`, zero scores skipped,
  operators and subscribers on their own boards, a store that fails reported.
- `tests/query/rank_service.cpp` — new. Each board name mapped, an unknown one refused, paging
  past the end, a clamped limit, an empty board answering 200.
- `tests/query/stats_service.cpp` — new. Totals on an empty and a filled hash, health with the
  store up and down, the key count passed through, the path bounds read from config.
- `tests/query/query_params.cpp` — new. Defaults, every parameter parsed, a clamped limit, a
  bad sort, a non-numeric limit.
- `tests/query/query_service.cpp` — weighted peers sorted by each metric, ties, paging past
  the end, an unweighted call answering what it answers today, path hops on a one-hop and a
  two-hop path, bytes reported unscaled.
- `tests/query/http_gateway.cpp` — the three new routes, parameters read off the query string,
  a parameterless request unchanged.
- `tests/store/redis_store.cpp` — `rank()` queued into the open batch and closed by the same
  flush.
- `tests/config.cpp` — the two new keys and their validation.

---

## 13. Documentation

- `HL_Design.md` — Query Service and Http Gateway rewritten for what they now answer, Store
  and Query Store for the new methods, and Rank Writer, Stats Service and Rank Service added
  in the same shape as the rest.
- `DL_Design.md` — the new signatures, the parameter parsing, the shared `Result`.
- `Class_Diagrams.md` — the three new classes in the aggregate and query diagrams.
- `Roadmap.md` — Phase 6's "Web UI, optional" becomes the UI these routes exist for.
- `config.toml` — the two new `[query]` keys, documented in the section's own prose.

---

## 14. Not doing

- **`/query/progress`.** The progress marks only exist on the file path; a rabbit run has
  none. A panel that is blank half the time is not a signal.
- **A run concept.** `total:proc` is lifetime and stays lifetime. The UI labels it.
- **Any `SCAN` route.** §4 explains what it would cost.
- **Ranking by ratio or by peer count.** §4 explains why neither is maintainable.
- **Detection and alerting.** §10 keeps the door open, nothing more.
- **CORS headers.** The browser never reaches the gateway.
- **Auth and rate limiting.** One client, on the same machine.
- **Batch lookups.** No screen fetches many subscribers at once.
- **A subgraph route.** Graph expansion is one weighted-peers call per expanded node, which is
  the interaction itself.
- **Operator names.** MCCMNC stays a number.
- **A config route.** The Python backend reads `config.toml` off disk itself.
- **Generator totals.** Emitted against consumed stays out of scope.
