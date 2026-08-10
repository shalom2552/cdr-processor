# CDR-Insight UI — Product Requirements

A web UI over the running processor. It shows what the store holds, answers a query about one
subscriber, one operator, one pair or a path, ranks the heaviest of them, and draws the
contact graph around a subscriber.

It reads. It never writes a counter, never edits config, never starts or stops a process.

Depends on the gateway changes in `query_api_changes.md`. Nothing here works until those land.

---

## 1. Scope

In:

- Dashboard over the store's totals, with rates over time.
- Query of a subscriber, an operator, a link, a path.
- Rankings: the heaviest subscribers and operators, off the sorted-set boards.
- Expandable contact graph around one subscriber.
- Read-only view of `config.toml`.
- Health of the gateway and the store.

Out:

- Control of anything. No start, no stop, no generator, no config edit.
- Any listing that would walk the keyspace. Rankings come from boards, never from a scan.
- Detection and alerting. Left possible, not built — §11.
- Ingest progress and file backlog. The progress marks exist only on the file path.
- Auth, accounts, roles.
- Log streaming.
- Emitted against consumed. The generator's counters stay where they are.

---

## 2. Users

One user, on the machine the stack runs on. Somebody showing what the processor does, or
watching a run go through. No shared deployment.

---

## 3. Architecture

```
browser (React, Vite)
   |  http, same origin
FastAPI (ui/api)
   |  http only, never Redis
C++ gateway
   |
Redis
```

Rules:

- The browser talks to FastAPI only. It never sees the gateway.
- FastAPI talks to the gateway over HTTP only. It opens no Redis connection. Anything the UI
  needs that the gateway cannot answer becomes a gateway route, not a Redis call in Python.
- FastAPI keeps one thing of its own: a sampler, on a timer, writing to a local SQLite file.
  That file is the only history in the system. The processor stores none.
- FastAPI reads `config.toml` off disk to render the config screen. A file read, not a data
  path.

What follows from that: history starts when the UI starts. Everything processed before it
existed is in the cumulative counters and nowhere on a curve.

---

## 4. Running it

The UI runs in Docker, beside redis and rabbit in `docker-compose.yml`. The gateway does not:
it stays a host process, started with `make query`.

So the container has to reach out to it. `[query].host` is `0.0.0.0`, a bind address — the
gateway listens on every interface. It is not something a client can dial, and inside a
container it means the container itself. The port is shared, the host is not.

One new key, `[ui] gateway_host`, holds where the gateway actually is:

- `host.docker.internal` in the container.
- `127.0.0.1` running the backend bare.

The port comes from `[query].port`, which is the port the gateway is listening on and the
port a client dials, the same number in both readings.

`config.toml` is mounted read-only into the container. It is the one configuration file for
the whole project and the UI reads it the way everything else does, at startup, with no
reload.

---

## 5. Config

A new `[ui]` section. Read by the Python backend and by nothing in C++, the way `[generator]`
is read only by the generator.

```toml
[ui]
gateway_host    = "host.docker.internal" # where the gateway is reached, port comes from [query]
api_port        = 8000    # port the ui backend listens on
sample_interval = 5       # seconds between samples
retention_days  = 7       # days of samples kept
db_path         = "ui/api/data/samples.db"
request_timeout = 5000    # milliseconds, gateway calls other than path
path_timeout    = 30000   # milliseconds, path search
peer_page       = 100     # peers per page in the peer list
top_page        = 20      # entries per page on a ranking board
expand_limit    = 50      # peers pulled per graph expansion
max_nodes       = 500     # nodes allowed on the graph canvas
```

---

## 6. Backend

FastAPI, one process, `ui/api`.

### Proxy

One endpoint per gateway route, same shapes, under `/api`. It exists so the browser stays
same-origin, so a dead gateway becomes a clean error, and so the timeouts live in one place.
It adds no data of its own.

| UI endpoint | Gateway route |
|---|---|
| `GET /api/health` | `/query/health` |
| `GET /api/totals` | `/query/totals` |
| `GET /api/top/{board}` | `/query/top/{board}?limit&offset` |
| `GET /api/subscriber/{msisdn}` | `/query/msisdn/{n}` |
| `GET /api/operator/{mccmnc}` | `/query/operator/{n}` |
| `GET /api/peers/{msisdn}` | `/query/link/{n}?weights=1&sort&limit&offset` |
| `GET /api/link/{a}/{b}` | `/query/link/{a}/{b}` |
| `GET /api/path/{a}/{b}` | `/query/path/{a}/{b}?weights=1` |

Statuses pass through. A gateway that does not answer is 502 with a body the UI can show. A
request that outlives its timeout is 504 — `/query/path` is the one that will.

### Sampler

A background task. Every `sample_interval` seconds it calls `/query/health` and
`/query/totals` and appends one row to SQLite: the timestamp, the fourteen counters, and the
key count.

- Rates are derived at read time, not stored: `(value - previous) / seconds`.
- Counters only rise. A drop means the store was flushed or replaced; the delta clamps to
  zero. The chart carries on flat rather than spiking negative. Cost of that choice, stated
  so nobody is surprised by it: a reset looks like a quiet minute, and cumulative totals
  restart without the chart saying so.
- A failed poll writes no row. A gap in the data is the truth about that minute.
- Retention `retention_days`, swept daily.

### Series

`GET /api/series?metric=records&window=1h`

Points for the dashboard charts, downsampled to what a chart can hold. Metrics are the
fourteen counters, their rates, and the averages derived from them.

### Config

`GET /api/config` returns `config.toml` parsed, section by section, in file order, each
section carrying the comment block above it as its help text. It also reports which sections
are live given `source.mode`, `source.format` and `store.type`, the way the file header
explains.

It reports the file. Not any running process.

---

## 7. Screens

Eight. Left rail, and a status pill in the header of every one.

### 7.1 Dashboard

The default screen. What the store holds, and what is moving right now.

Every cumulative number on it is **lifetime**, not this run: `total:proc` is incremented by
every run since the store was created and is never reset, and the processor's own `RunTotals`
never leaves its memory. Tiles say "since store creation" and mean it. Rates are unaffected —
they are deltas between samples, so the throughput chart reads the same either way.

- **Status strip.** Gateway up or down, store up or down, sampler running, age of the last
  sample. An age over two intervals turns it amber. A dead gateway turns it red and every
  panel below goes stale rather than empty.
- **KPI tiles.** Records, calls (`moc + mtc`), messages (`sms-mo + sms-mt`), data sessions,
  data volume (`rx + tx`, formatted), failure share (`(noans + busy + failed) / records`),
  keys in store. Each carries its rate over the window as a sparkline and a delta.
- **Throughput.** Records per second over time. Window picker: 15m, 1h, 6h, 24h, all. The
  chart the sampler exists for.
- **Usage mix.** The eight usage types as a share of records: a donut for now, a stacked area
  for the mix over time.
- **Outcomes.** Answered against no-answer, busy and failed, over time, so a bad stretch of a
  run is a shape rather than a number.
- **Durations.** Average outgoing call seconds, average incoming, average data session.
  Derived, three tiles with a trend.
- **Leaders.** Top five on each subscriber board — `voice`, `sms`, `data`, `fail` — as four
  short lists, and top five operators by `op-voice`. One `/api/top` call each, no scan. A row
  opens that subscriber.
- **Empty state.** Before two samples exist there are no rates. Tiles show the absolute
  totals, and the charts say how long until the first curve. Never a flat zero line.

`keys in store` is labelled as what it is: every key together — subscribers, operators, link
hashes, the boards and the totals hash. Not a subscriber count.

### 7.2 Subscriber

- **Entry.** A search box at the top for speed, tabs below it for precision. The box takes
  digits: one number is a subscriber, two are a link and a path. The tabs are Subscriber,
  Operator, Link, Path, each with its own inputs and no guessing.
- **Result card.** The nine counters, formatted: seconds as `h m s`, bytes as MB or GB, counts
  bare.
- **Derived.** Out against in for voice, sms and data, as paired bars. This subscriber's
  failure rate against the store's average, so one number has something to be read against.
  Average call length, and peers against calls — the shape a heavy fan-out shows up in.
- **Peers.** The weighted peer list, paged at `peer_page`, sortable by duration or by
  messages, with the total count shown whether or not it was all fetched. A row opens the
  link view, or sends that peer to the graph.
- **Not found.** 404 is a normal answer: never seen in any processed record. The graph and
  path entry points stay offered — a typo is the usual cause, not an empty store.

### 7.3 Rankings

The only screen that answers "who should I be looking at".

- Board picker: `voice`, `sms`, `data`, `fail`, and the two operator boards `op-voice` and
  `op-sms`. One at a time.
- A ranked table: position, id, score formatted for that board's unit, and the share of the
  board's leader. Paged at `top_page`, with the board's cardinality shown.
- A row opens the subscriber, or sends it straight to the graph.
- **What the boards are not.** They rank sums, not ratios. `fail` is the largest absolute
  count of no-answer, busy and failed — a heavy user will out-rank a suspicious one. The
  screen says this next to that board rather than letting the number be misread. There is no
  board for peer count either; see the note in `query_api_changes.md` §4.
- Empty board is a normal state before anything has been processed.

### 7.4 Graph

The contact graph around one subscriber. Force directed, canvas.

- The centre is the queried subscriber. Its peers come from one weighted call.
- Edge thickness is the selected metric, call seconds or messages. One toggle, and the legend
  restates it — thickness alone reads as nothing.
- Node size is the sum of its **known** edge weights. Known, not true: the graph knows only
  the edges it has fetched, and the UI must not imply otherwise.
- **Expand.** Click a node, fetch its weighted peers, merge. A node already on the canvas
  joins up as an edge instead of appearing twice, which is how a cluster becomes visible.
- **Hub guard.** Expansion pulls `expand_limit` peers, heaviest first. A node with more says
  "showing 50 of 812" and offers the paged list instead of emptying itself onto the canvas.
- **Canvas cap.** `max_nodes` on screen. At the cap, an expansion is refused with a reason,
  not a freeze.
- **Selection.** A selected node shows its subscriber counters, fetched on demand. A hovered
  edge shows its duration and messages.
- Controls: metric toggle, expand limit, freeze layout, reset to centre, remove a node.

### 7.5 Path

- Two inputs, or arrived at from the search box with two numbers.
- The chain, left to right, every hop labelled with its duration and messages.
- Below it, the two parties' direct link if they have one. A path of length one and a link
  are the same fact, shown both ways.
- **Not found** states the bounds instead of shrugging: at most `max-hops` hops and
  `max-visited` subscribers read. Both come from `/query/health`, so the screen never states a
  limit that has since been retuned.
- A path can take seconds. A spinner with a cancel, and a 504 reported as a timeout — not as
  "no path". Those are different answers.

### 7.6 Operator

- Lookup by MCCMNC. Four counters, out against in for voice and for messages.
- Share of the store: this operator's voice seconds against `moc-dur + mtc-dur`.
- No picker, and no way to list every operator seen. The `op-voice` and `op-sms` boards rank
  the heaviest, which is not the same as enumerating them. Otherwise the user types a code,
  and the screen says so rather than hiding the gap.

### 7.7 Config

- `config.toml` by section, in file order, each under the prose that documents it in the
  file.
- Sections not selected by `source.mode`, `source.format` and `store.type` are dimmed and
  marked inactive.
- A banner: this is the file on disk. Config is read once at startup and never reloaded, so a
  process started before the last edit is running something else.
- Read-only. No edit affordance anywhere, not even a disabled one.

### 7.8 System

- `/query/health` in full: gateway state, store state, keys, path bounds.
- The gateway address it was reached at, and the store address from config.
- Sampler: interval, rows stored, oldest row, database size, last error.
- The gateway routes the UI depends on, each with its last status. The screen a broken install
  is diagnosed from.

---

## 8. Across every screen

- **Query history.** Recent lookups in the rail, click to re-run. Browser storage, no
  backend. Worth its cost the first time a graph walk needs retracing.
- **Export.** A chart as PNG, a table as CSV, the current graph as JSON.
- **Formatting.** Seconds, bytes and counts are formatted in one place and one way
  everywhere.

---

## 9. Non-functional

- **Latency.** Proxy overhead under 10 ms. Every screen but path answers inside 200 ms against
  a warm store. Path is the only one allowed a spinner over a second.
- **Failure.** Three failures are distinct and must never render alike: the gateway is down
  (502, red banner, everything stale), the store is down (503, "store unavailable", the UI
  itself is fine), the key does not exist (404, empty state). A UI that shows "no data" for
  all three is wrong.
- **Staleness.** Every number carries the age of the sample behind it. A frozen dashboard has
  to look frozen.
- **Load.** The sampler polls one `/query/health` and one `/query/totals` per interval, no
  matter how many tabs are open: it runs server side and the tabs read what it wrote. The
  boards are not sampled — a dashboard load costs its five `/api/top` calls, and a Rankings
  page costs one per board change. Those are `ZREVRANGE`s, O(log n + k), so the cost is
  per-view and small.
- **Security.** No auth. The API port binds `127.0.0.1`, and compose publishes it there. The
  UI must not be the thing that exposes the gateway.
- **Theme.** Dark and light. Colour is never the only encoding on a chart.

---

## 10. Layout

```
ui/
  api/     FastAPI, sampler, sqlite
  web/     React, TypeScript, Vite
```

- Dev: a script under `scripts/`. The Makefile builds the C++ project and nothing else.
- Deployed: services in `docker-compose.yml` beside redis and rabbit, `config.toml` mounted
  read-only.
- The gateway stays where it is. The UI is a client of it, not a wrapper around it.

Order, largest risk first:

1. The gateway changes, boards included. Nothing else can start.
2. Backend: proxy, sampler, series, config.
3. Dashboard.
4. Subscriber and peers.
5. Rankings.
6. Graph.
7. Path.
8. Operator, Config, System.
9. History, export, polish.

---

## 11. Left open for detection

Monitoring for abnormal behaviour — a number calling far too many people, one pair far too
often — is **not** built here. This section is the check that nothing built here makes it
harder later.

What is already in place:

- The boards answer "who is extreme at X" without a keyspace walk, which is the hard half of
  finding a candidate at all.
- The subscriber screen already computes the ratios a rule would use: failure share, out
  against in, average call length, peers against calls. Today they are read by a person.
- The graph and the peer weights show concentration on one pair directly.

What is deliberately missing, and what it would cost:

- **Somewhere to keep an alert.** The backend has a database for samples; alerts would be
  another table, plus a screen, plus an ack.
- **A time dimension per entity.** Lifetime counters describe a shape, never a change. They
  can say "this number calls 400 people and answers nobody". They cannot say "this started an
  hour ago". Bursts need either time-bucketed keys in Redis or a detector holding its own
  windows.
- **Ranking by ratio.** The boards rank sums. "Worst failure rate" is a division and is not
  maintainable in a sorted set — it would need a scan, or a score the processor computes and
  writes itself.

None of that is blocked by anything in this document.

---

## 12. Open questions

1. **First run against an empty store.** A store with no keys is hard to tell from a store
   nobody has queried right. `/query/health` gives the key count, so the dashboard can say
   "the store is empty" with confidence — worth a first-run state of its own.
2. **Sampling while nothing runs.** With the processor stopped, the sampler writes identical
   rows every five seconds forever. Skip a row when nothing moved, or keep the cadence and let
   retention deal with it?
3. **Graph state on reload.** Deep links were not taken, so an expanded graph is lost on
   refresh. Fine for now; revisit if the graph becomes the screen people live on.
4. **Windows longer than retention.** The "all" window means seven days. Either say so on the
   picker or drop the option.
