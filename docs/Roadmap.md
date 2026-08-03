
# CDR-Insight — Roadmap
 
Distributed C++ CDR processor: parallel ingest, subscriber/operator aggregation, contact graph, REST query API.
 
---
 
## Phase 0 — Skeleton
**Goal:** buildable repo, nothing works yet.
 
- [x] Repo layout: `src/ inc/ tests/ scripts/ docs/`, `config.toml` at the root
- [x] Makefile (`all`, `test`, `clean`, `debug`, `release`), C++17, `-Wall -Wextra -pedantic`
- [x] Unit test harness wired (doctest / gtest, header-only preferred)
- [x] CI: build + test on push
**Done when:** `make && make test` is green on an empty test suite.
 
---
 
## Phase 1 — Core Infrastructure
**Goal:** reusable primitives, all unit-tested in isolation.
 
- [x] `Config` — INI-style parser, typed getters, defaults, validation on load
- [x] `Logger` — leveled, thread-safe, async sink
- [x] `ThreadPool` — fixed workers, bounded MPMC task queue (backpressure, not unbounded growth)
- [x] `CdrRecord` — POD struct; IMSI/MSISDN as `uint64_t`, not string
- [x] `ICdrParser` + `PipeDelimitedParser` — tokenize `|`, field validation, reject malformed
- [ ] `ParserRegistry` — format name → parser factory (satisfies §8.2 "future change of file format")
**Done when:** parser round-trips a 10k-line fixture with zero allocations per record beyond the record itself.
 
**Warning:** decide IMSI/MSISDN storage now. Switching from `std::string` to integer keys later touches every map, every hash, every test.
 
---
 
## Phase 2 — Ingest
**Goal:** files in → records streamed to a sink.
 
- [x] `ICdrSource` interface — `bool next(std::vector<CdrRecord>&)`
- [ ] `DirWatchSource` — inotify on input dir, stable-file detection (size settled / rename-on-complete)
- [x] Streaming reader — mmap or chunked `read()`; **never load a 5 GB file into memory**
- [ ] Concurrent multi-file: 2–4 files in flight, one worker per file
- [ ] Move to `done/` on success, `failed/` on parse error
- [x] `scripts/cdr_pipe_generator.py` — generates synthetic CDR files at configurable rate/size
**Done when:** the generator drops 4× 1 GB files; all are consumed, moved, and record count matches exactly.
 
**Question:** rename-into-directory or size-polling for completion detection? Rename is atomic and race-free — push for it if you control the delivery side.
 
---
 
## Phase 3 — Aggregation Engine
**Goal:** the actual product.
 
- [ ] `BillingAggregator` — per-IMSI: voice in/out sec, data rx/tx KB, SMS in/out, per-second-party breakdown
- [ ] `OperatorAggregator` — per MCC/MNC: voice in/out sec, SMS in/out
- [ ] `LinkGraph` — adjacency map, edge weight = `(total_duration, sms_count)`
- [ ] Concurrency: sharded maps keyed by `hash(IMSI) % N`, one lock per shard — not one global mutex
- [ ] Merge step: per-thread local accumulators → periodic merge into shared state
**Done when:** aggregates from a known fixture match a Python reference implementation byte-for-byte.
 
**Heads-up:** this is the phase that gets scrutinized. Benchmark the lock strategy before committing to it — a global mutex here caps you at single-core throughput regardless of how many readers you run.
 
---
 
## Phase 4 — Query Gateway
**Goal:** REST/JSON over HTTP GET, no auth.
 
- [ ] Embedded HTTP server (cpp-httplib or hand-rolled on the thread pool)
- [ ] `GET /query/msisdn/{msisdn}` → billing aggregate
- [ ] `GET /query/operator/{mccmnc}` → operator aggregate
- [ ] `GET /query/link/{a}/{b}` → edge attributes
- [ ] `GET /query/path/{a}/{b}` → BFS shortest path across the graph
- [ ] Uniform JSON error object; correct HTTP status codes
- [ ] `query-concurrency` from config caps handler threads
- [ ] Reader/writer separation: queries must not block ingest
**Done when:** all four endpoints answer correctly *while* ingest is running at full rate.
 
---
 
## Phase 5 — Persistence
**Goal:** survive restarts. Spec calls this phase 2 — don't start it early.
 
- [ ] MySQL schema: `subscriber_agg`, `operator_agg`, `links`
- [ ] Dump on graceful shutdown (SIGTERM handler)
- [ ] Load on startup, batched inserts/selects
- [ ] Idempotent — reload must reproduce identical in-memory state
**Done when:** shutdown → restart → queries return the pre-shutdown answers.
 
---
 
## Phase 6 — Distribution & Clients
**Goal:** the tier split from §8.1.
 
- [ ] Harvester ↔ processor transport (custom TCP protocol, or `AmqpSource` behind `ICdrSource`)
- [ ] Harvester runs standalone on its own host, N instances
- [ ] Console client — REST calls, formatted output
- [ ] (Optional) minimal web UI
**Done when:** 3 harvesters on separate hosts feed 1 processor; console client queries from a 4th.
 
---
 
## Phase 7 — Hardening & Deliverables
**Goal:** shippable.
 
- [ ] Profile: `perf` for hotspots, `valgrind --tool=massif` for allocation churn
- [ ] Sanitizers clean: ASan, TSan, UBSan
- [ ] Throughput target measured and documented (records/sec, MB/sec)
- [ ] Unit tests on all infrastructure modules
- [ ] HLD PDF — block diagram
- [ ] DLD PDF — class diagram, per-module threading requirements, sequence diagrams
- [ ] README — build, configure, operate
- [ ] Config file format documented
---
 
## Cross-Cutting Rules
 
- **Interfaces before implementations.** `ICdrSource` / `ICdrParser` exist from Phase 1 so Phase 6 is a plug-in, not a rewrite.
- **No premature MQ.** `DirWatchSource` is the graded path. `AmqpSource` slots in behind the same interface if and when it's justified.
- **Never move bulk data through a broker.** File paths or parsed batches only.
- **Measure before optimizing.** Every perf claim in the DLD needs a number behind it.
## Sequencing Note
 
Phases 1→2→3 are strictly serial. Phase 4 can start once Phase 3's read API is defined, before it's fast. Phases 5 and 6 are independent of each other. Phase 7 is continuous, not final.
