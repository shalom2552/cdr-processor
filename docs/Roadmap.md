# CDR-Insight — Roadmap

Distributed C++ CDR processor: parallel ingest, subscriber/operator aggregation, contact
graph, REST query API.

---

## Phase 0 — Skeleton

- [x] Repo layout, `config.toml` at the root
- [x] Makefile, C++17, warnings on
- [x] doctest harness wired
- [x] CI: build and test on push

---

## Phase 1 — Core Infrastructure

- [x] `Config` — toml, typed getters, validated on load
- [x] `Logger` — leveled, thread safe
- [x] `ThreadPool` — fixed workers, bounded queue
- [x] `CdrRecord` — IMSI and MSISDN as `uint64_t`
- [x] `IParser` and `CsvParser`
- [x] `ParserFactory` — format name to parser

---

## Phase 2 — Ingest

- [x] `ICdrSource` interface
- [x] `DirWatcher` — inotify, rename on complete
- [x] `MappedFile` — never load a whole file
- [x] Several files in flight, one reader each
- [x] Moved to `done/` or `failed/`
- [x] `RabbitSource` and `RabbitIngestor` — one connection per consumer
- [x] `generator/` — synthetic records at a configurable rate

---

## Phase 3 — Aggregation Engine

- [x] `Aggregator` — batch into a `Delta`, no locks, no I/O
- [x] Subscriber per MSISDN — voice, data, sms
- [x] Operator per MCCMNC, from the subscriber's own IMSI
- [x] Links — one hash per subscriber, duration and sms per peer
- [x] `IStore` and `RedisStore` — one connection per thread, pipelined `HINCRBY`
- [x] `AggregateWriter` — a `Delta` and a `Totals` into the store
- [x] `Totals` and `RunTotals` — both sides count the same fourteen fields
- [x] `RedisSink` — fold then write, per thread delta
- [x] Redis host, port and timeout in `config.toml`

---

## Phase 4 — Query Gateway

- [ ] Embedded HTTP server
- [ ] `GET /query/msisdn/{msisdn}`
- [ ] `GET /query/operator/{mccmnc}`
- [ ] `GET /query/link/{a}/{b}`
- [ ] `GET /query/path/{a}/{b}` — BFS over the links
- [ ] JSON errors, correct status codes
- [ ] `query-concurrency` caps handler threads

---

## Phase 5 — Persistence

- [ ] RDB and AOF policy, fsync setting written down
- [ ] `maxmemory`, no eviction on aggregate keys
- [ ] Progress keys beside the counters they belong to
- [ ] Restart drill: `kill -9` mid file, totals still right

**Warning:** §7 asks for MySQL. Redis persistence replaces it — a deviation to defend.

---

## Phase 6 — Distribution & Clients

- [ ] Harvester — ingest and write, no gateway
- [ ] Processor — gateway only
- [ ] Harvesters share one Redis
- [ ] Source id per harvester, so progress keys never collide
- [ ] Sequence numbers unique across harvesters
- [ ] Console client
- [ ] Web UI, optional

---

## Phase 7 — Hardening & Deliverables

- [ ] Reconnect with backoff
- [ ] Pipeline depth and batch size benchmarked
- [ ] `perf` for hotspots, massif for allocation churn
- [ ] ASan, TSan and UBSan clean
- [ ] Throughput measured and written down
- [ ] Unit tests on every module
- [ ] HLD block diagram, DLD class and sequence diagrams
- [ ] README — build, configure, operate

---

## Known Limitations

- A slow Redis blocks a reader, fills the pool queue, and stalls the watcher.
- A half-failed pipeline leaves a batch partly counted. No rollback.
- One Redis: single point of failure, single-threaded write bottleneck.

---

## Cross-Cutting Rules

- Interfaces before implementations.
- Never move bulk data through a broker — paths or parsed batches only.
- Measure before optimizing.

Phases 1→2→3 are serial. Phase 4 starts once the key schema is fixed. Phases 5 and 6 are
independent. Phase 7 is continuous.
