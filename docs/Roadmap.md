
# CDR-Insight — Roadmap
 
Distributed C++ CDR processor: parallel ingest, subscriber/operator aggregation, contact graph, REST query API.
 
---
 
## Phase 0 — Skeleton
**Goal:** buildable repo, nothing works yet.
 
- [x] Repo layout: `src/ inc/ tests/ generator/ scripts/ docs/ third_party/`, `config.toml` at the root
- [x] Makefile (`all`, `test`, `clean`, `debug`, `release`), C++17, `-Wall -Wextra -pedantic`
- [x] Unit test harness wired (doctest / gtest, header-only preferred)
- [x] CI: build + test on push
**Done when:** `make && make test` is green on an empty test suite.
 
---
 
## Phase 1 — Core Infrastructure
**Goal:** reusable primitives, all unit-tested in isolation.
 
- [x] `Config` — toml parser, typed getters, defaults, validation on load
- [x] `Logger` — leveled, thread-safe, async sink
- [x] `ThreadPool` — fixed workers, bounded MPMC task queue (backpressure, not unbounded growth)
- [x] `CdrRecord` — POD struct; IMSI/MSISDN as `uint64_t`, not string
- [x] `ICdrParser` + `CsvParser` — tokenize `|`, field validation, reject malformed
- [x] `ParserRegistry` — format name → parser factory (satisfies §8.2 "future change of file format")
**Done when:** parser round-trips a 10k-line fixture with zero allocations per record beyond the record itself.
 
**Warning:** decide IMSI/MSISDN storage now. Switching from `std::string` to integer keys later touches every map, every hash, every test.
 
---
 
## Phase 2 — Ingest
**Goal:** files or a queue in → records streamed to a sink.
 
- [x] `ICdrSource` interface — `Status next(std::vector<CdrRecord>&)`
- [x] `DirWatchSource` — inotify on input dir, stable-file detection (size settled / rename-on-complete)
- [x] Streaming reader — mmap or chunked `read()`; **never load a 5 GB file into memory**
- [x] Concurrent multi-file: 2–4 files in flight, one worker per file
- [x] Move to `done/` on success, `failed/` on parse error
- [x] `RabbitSource` / `RabbitIngestor` — one connection per consumer thread, a batch acked once consumed
- [x] `generator/` — python package that generates synthetic CDR files at configurable rate/size
**Done when:** the generator drops 4× 1 GB files; all are consumed, moved, and record count matches exactly.
 
**Question:** rename-into-directory or size-polling for completion detection? Rename is atomic and race-free — push for it if you control the delivery side.
 
---
 
## Phase 3 — Aggregation Engine
**Goal:** the actual product. State lives in Redis, not in memory.
 
- [ ] `Aggregator` — folds a batch into a `Delta` of increments; no Redis, no locks, no I/O
- [ ] Billing per MSISDN: voice in/out sec, data rx/tx, SMS in/out, per-second-party breakdown
- [ ] Operator per MCC/MNC, from the subscriber's own IMSI: voice in/out sec, SMS in/out
- [ ] Links: one hash per subscriber, duration and sms per peer — not one key per edge
- [ ] `U`, `B`, `X` counted nowhere (§2.1)
- [ ] `IStore` / `RedisStore` — applies a `Delta`, hiredis, one connection per thread, pipelined `HINCRBY`
- [ ] `RedisSink : ISink` — fold then apply, synchronous
- [ ] Redis host, port and timeouts in `config.toml`
- [ ] Idempotency: per-source high-water sequence, written in the batch's pipeline
**Done when:** aggregates from a known fixture match a Python reference implementation, and a replay of it changes nothing.
 
**Warning:** the generator saves its counter only on a clean exit and shares one file across processes, and 2–4 files in flight means sequences arrive out of order. Both break the high-water mark.
 
---
 
## Phase 4 — Query Gateway
**Goal:** REST/JSON over HTTP GET, no auth.
 
- [ ] Embedded HTTP server (cpp-httplib or hand-rolled on the thread pool)
- [ ] `GET /query/msisdn/{msisdn}` → billing aggregate
- [ ] `GET /query/operator/{mccmnc}` → operator aggregate
- [ ] `GET /query/link/{a}/{b}` → edge attributes
- [ ] `GET /query/path/{a}/{b}` → BFS across the link hashes
- [ ] Uniform JSON error object; correct HTTP status codes
- [ ] `query-concurrency` from config caps handler threads
- [ ] Handlers read Redis directly, one connection each
**Done when:** all four endpoints answer correctly *while* ingest is running at full rate.
 
**Heads-up:** reader/writer separation comes free — queries never touch the ingest path.
 
---
 
## Phase 5 — Persistence
**Goal:** survive restarts. Spec calls this phase 2 — don't start it early.
 
- [ ] RDB and AOF policy chosen, fsync setting written down
- [ ] Data dir, `maxmemory`, and no eviction on aggregate keys
- [ ] Progress keys persisted with the counters they belong to
- [ ] Restart drill: `kill -9` mid-file, restart, totals still correct
**Done when:** shutdown → restart → queries return the pre-shutdown answers, and a replayed file does not double count.
 
**Warning:** §7 asks for MySQL. Redis persistence replaces it — a deviation to defend, not to hide.
 
---
 
## Phase 6 — Distribution & Clients
**Goal:** the tier split from §8.1.
 
- [ ] Harvester: ingest, aggregate, write. No query gateway
- [ ] Processor: query gateway only
- [ ] Harvesters share one Redis — no transport to build
- [ ] Per-harvester source id in config, so progress keys never collide
- [ ] Sequence numbers unique across harvesters — shared counter or per-harvester prefix
- [ ] Console client — REST calls, formatted output
- [ ] (Optional) minimal web UI
**Done when:** 3 harvesters on separate hosts write to one Redis; console client queries from a 4th.
 
---
 
## Phase 7 — Hardening & Deliverables
**Goal:** shippable.
 
- [ ] Redis timeouts enforced, reconnect with backoff
- [ ] Pipeline depth and batch size benchmarked
- [ ] Profile: `perf` for hotspots, `valgrind --tool=massif` for allocation churn
- [ ] Sanitizers clean: ASan, TSan, UBSan
- [ ] Throughput target measured and documented (records/sec, MB/sec)
- [ ] Unit tests on all infrastructure modules
- [ ] HLD PDF — block diagram
- [ ] DLD PDF — class diagram, per-module threading requirements, sequence diagrams
- [ ] README — build, configure, operate
- [ ] Config file format documented
**Done when:** sanitizers clean, numbers measured, documents written.
 
---
 
## Known Limitations
 
- A slow Redis blocks a reader, fills the pool queue, and stalls the watcher. Needs timeouts.
- A half-failed pipeline leaves a batch partly counted. No rollback.
- One Redis: single point of failure, single-threaded write bottleneck.
 
---
 
## Cross-Cutting Rules
 
- **Interfaces before implementations.** `ICdrSource` / `ICdrParser` exist from Phase 1 so Phase 6 is a plug-in, not a rewrite.
- **No premature MQ.** `DirWatchSource` is the graded path. `AmqpSource` slots in behind the same interface if and when it's justified.
- **Never move bulk data through a broker.** File paths or parsed batches only.
- **Measure before optimizing.** Every perf claim in the DLD needs a number behind it.
## Sequencing Note
 
Phases 1→2→3 are strictly serial. Phase 4 can start once Phase 3's key schema is fixed, before it's fast. Phases 5 and 6 are independent of each other. Phase 7 is continuous, not final.
