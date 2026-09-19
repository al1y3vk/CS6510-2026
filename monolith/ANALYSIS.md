# Monolith — Architectural Analysis

Week 1: one C++20 process (Drogon 1.8.7) in front of one Postgres 18. Baskets live in
RAM; a sale becomes durable in a single SQL function call at completion. Everything a
request needs is either a hash lookup in memory or exactly one round-trip to the DB.

| Request | RAM | DB round-trips |
| --- | --- | --- |
| `GET /items` | catalog (read-only after startup) | 0 |
| `POST /transactions` | create basket | 0 |
| `POST /transactions/{id}/items` | append to basket, push into hopping window | 0 (every 500th scan fires one async insert, never awaited) |
| `POST /transactions/{id}/complete` | mark basket `Completing`, then remove | 1: `SELECT complete_sale(...)` = one transaction |
| `GET /transactions/{id}` | open basket | 1 only if not open (look up `sales`) |
| `GET /inventory/low-stock` | – | 1 |
| `GET /analytics/popular-items` | – | 1 (newest snapshot row) |

Hardware for every number below: Intel Core Ultra 9 185H (22 threads), 30 GB RAM, NVMe
SSD, Postgres 18.6 in Docker on the same laptop as the server and the load client.

## 1. Architectural characteristics and concrete requirements

| Characteristic | Concrete requirement | Result |
| --- | --- | --- |
| **Performance** | At 10 stations: p99 start and scan < 5 ms, p99 complete < 100 ms | 0.53 ms, 0.47 ms, 91 ms — met |
| **Scalability** | 10× the stations (100) completes with 0 errors and no drop in throughput; only DB-bound operations may slow down | 0 errors, throughput +6 %, start/scan unchanged, complete ×20 — met, with a clear ceiling (§2) |
| **Data integrity** | After every run, for every SKU `initial − final = LEAST(units sold, initial)` and stock ≥ 0 (`db/verify.sql` returns 0 rows). A 200 on complete is sent only after the commit | `INVARIANT OK` on every run — met |
| **Reliability** | Server survives a full run without restart; a DB failure during complete returns 500 and leaves the basket `Open` so the station can retry; startup refuses to serve an empty catalog | Met by design; DB-failure path is unit-tested only by inspection (not injected under load) |
| **Testability** | Business logic testable without a database in < 1 s; DB logic testable against a 5-item DB; whole system testable with the real load client in one command | 19 unit tests (0.07 s), 9 DB tests (1.5 s), `tests/smoke.sh`, `scripts/run-test.sh` — met |
| **Deployability** | One binary, one config file, one compose file; a fresh machine is running in 4 commands | `docker compose up -d --wait`, `db/reset.sh`, `cmake --preset release && cmake --build --preset release`, `scripts/run-test.sh` — met |
| **Simplicity** | Fits in one head: ~1,500 lines of C++ including tests, no ORM, five SQL statements in total, two static libraries with one-directional dependencies | Met (`checkout_core` has no Drogon dependency; `checkout_db` sits on top of it) |

## 2. Priorities and trade-offs

Ranked by what the measurements say matters: **data integrity, performance, testability.**
Integrity is first because the grading invariant is binary (any drift is a fail), and
because the concurrency gotcha in the assignment is exactly this bug. Performance is
second because the results show it is where this architecture's real limit is.
Testability is third because it is what made the other two verifiable rather than
hoped for.

### 2.1 Baskets in RAM — performance vs. reliability

Start and scan never touch the database. That is why their p50 is 0.14–0.18 ms at both
10 and 100 stations: the number is the localhost HTTP round-trip, the server's own work
is microseconds. The cost is that open baskets are volatile: a server crash loses every
basket that has not completed. Completed sales are unaffected (they are committed before
the 200). For a self-checkout this is acceptable — the customer still has the items in
hand and can rescan — and it is the single biggest reason the system does 5,000+ scans/s.

### 2.2 Completion as one SQL function — performance + correctness vs. logic-in-DB

`complete_sale()` locks the basket's stock rows in SKU order (`FOR UPDATE`, so two
overlapping baskets can never deadlock), decrements, writes the `sales` row and the
`receipt_lines`, and returns the commit timestamp. One statement is one implicit
transaction and one round-trip: when the call returns, the sale is durable, and the
handler sends the 200. This is what makes the invariant hold under load with no
application-level locking at all.

The trade-off is that inventory logic lives in the database. It is not portable to
another RDBMS, it is not unit-testable without Postgres (hence the `db` test label),
and in the service-based week it will have to move into an inventory service. I took
that trade knowingly: the alternative (BEGIN / SELECT FOR UPDATE / UPDATE / INSERT /
INSERT / COMMIT from C++) is five round-trips and, in Drogon 1.8.7, has no awaitable
commit, so "200 only after COMMIT" would need hand-written plumbing.

### 2.3 The hot row — what actually limits throughput

Both graded runs cap at ~500–540 completes/s regardless of station count. Little's law
on the closed-loop client explains where the time goes: at 100 stations each station
spends 185 ms per basket and 183 ms of that is inside `complete`. Adding stations adds
queue, not throughput.

The serial resource is one row. The load client's item choice is Zipf-distributed, and
in the 100-station run **66.8 % of all baskets contained SKU-000001** (89.8 % contained
one of the top 8). Every one of those completes must hold the row lock on SKU-000001
from `FOR UPDATE` until commit, and commit waits for the WAL fsync
(`synchronous_commit = on`, the Postgres default). Sixteen DB connections cannot help;
fifteen of them are waiting on the lock.

Evidence: re-running the 100-station test with `synchronous_commit = off` — same code,
same load, one DB setting — gave **2.7× the throughput** (539 → 1,472 tx/s) and cut every
complete percentile by the same factor. The fsync was inside the lock hold time. The
price is that Postgres then flushes WAL up to 3 × `wal_writer_delay` (600 ms) after
the commit returns, so a DB crash could lose up to ~880 sales the client already got a
200 for. The graded runs keep the default because §2.2's guarantee ("200 means durable")
is worth more than throughput at this scale; the experiment is here to quantify what
that guarantee costs.

### 2.4 Floor-at-zero — spec constraints vs. strict accounting

The client sells the top items far past their stock (SKU-000001: 83,428 units against
10,000 in the 100-station run). Stock never goes negative; receipt lines record every
unit sold; `verify.sql` checks `decremented = LEAST(sold, initial)`. Strict accounting
would reject the sale, but the assignment states the customer already holds the item, so
the backend's job is to count, not to gate.

### 2.5 Fire-and-forget snapshot — performance vs. a possibly missing snapshot

Every 500th scan computes the top-N (about 10 µs over 2,000 counters under one mutex)
and hands the insert to the DB client without awaiting it. The scan that triggers it
pays nothing measurable (scan p99 is 0.38–0.47 ms). If the server dies in the ~1 ms
before the insert lands, one snapshot is lost and the next slide replaces it 500 scans
later. `GET /analytics/popular-items` reads the newest stored row, so the analytics
endpoint never touches the in-memory window and never contends with scans.

## 3. Results

Both runs: `scripts/run-test.sh` (reset DB → start server → load client → `verify.sql`).
Reports: `reports/report-20260917-155720.json` (default) and
`reports/report-20260917-160254.json` (stress). Postgres at defaults.

### Default: 10 stations, 60 s

| | OK | Errors | Mean | p50 | p95 | p99 | Max |
| --- | --- | --- | --- | --- | --- | --- | --- |
| START_TRANSACTION | 30,450 | 0 | 0.21 ms | 0.17 | 0.34 | 0.53 | 33.3 |
| SCAN_ITEM | 320,763 | 0 | 0.16 ms | 0.14 | 0.28 | 0.47 | 38.8 |
| COMPLETE_TRANSACTION | 30,450 | 0 | 17.8 ms | 8.18 | 64.8 | 91.2 | 576 |

Throughput 507 tx/s, 5,344 scans/s. Low-stock alerts: 3 (SKU-000001..3 at 0).
`INVARIANT OK (0 mismatches)`.

### Stress: 100 stations, 120 s

| | OK | Errors | Mean | p50 | p95 | p99 | Max |
| --- | --- | --- | --- | --- | --- | --- | --- |
| START_TRANSACTION | 64,808 | 0 | 0.28 ms | 0.18 | 0.29 | 0.47 | 68.1 |
| SCAN_ITEM | 679,803 | 0 | 0.18 ms | 0.14 | 0.23 | 0.38 | 49.0 |
| COMPLETE_TRANSACTION | 64,808 | 0 | 183 ms | 164 | 282 | 424 | 1,267 |

Throughput 539 tx/s, 5,656 scans/s. Low-stock alerts: 8 (SKU-000001..8 at 0).
`INVARIANT OK (0 mismatches)`.

### Experiment (not graded): 100 stations, 120 s, `synchronous_commit = off`

| | OK | Errors | Mean | p50 | p95 | p99 | Max |
| --- | --- | --- | --- | --- | --- | --- | --- |
| START_TRANSACTION | 176,755 | 0 | 0.25 ms | 0.18 | 0.34 | 1.41 | 58.1 |
| SCAN_ITEM | 1,855,127 | 0 | 0.21 ms | 0.16 | 0.30 | 1.18 | 57.6 |
| COMPLETE_TRANSACTION | 176,755 | 0 | 65.4 ms | 59.5 | 107 | 154 | 428 |

Throughput 1,472 tx/s, 15,450 scans/s. Low-stock alerts: 22. `INVARIANT OK (0 mismatches)`.
Report: `reports/experiments/sync-commit-off-100x120.json`.

### Reading the numbers

- **Start and scan are flat across all three runs** (p50 0.14–0.18 ms, p99 under 1.5 ms).
  Nothing that happens in the database reaches them.
- **Complete is the whole story.** 10 → 100 stations: throughput +6 %, p50 ×20. That is a
  saturated serial resource (§2.3), not a server that is running out of CPU or threads.
- **Tails.** Complete max 576 ms / 1.27 s against p99 of 91 / 424 ms: rare stalls
  consistent with WAL flush or checkpoint activity on the DB side, plus GC pauses in the
  Java client, which measures latency from its side. Start/scan maxima of 33–68 ms with
  p99 under 1 ms are the same kind of outlier.
- **Popular items are stable across runs** (SKU-000001 ≈ 13–14 % of the last 1,000 scans
  every time, ranks 1–3 identical, ranks 4–10 shuffle within noise for counts of 12–40).
  The window is hopping, not cumulative, so run length does not change it — which is
  what the assignment says to look for.
- **Low-stock grows with units sold** (3 → 8 → 22 SKUs at 0) and every alert is at
  exactly 0: the Zipf tail falls off so fast that no item lands between 1 and 49.

## 4. What this week tells the next ones

The monolith's limit is not the process, the threads or the HTTP layer; it is one
Postgres row and one fsync per sale. Any later architecture that keeps a single
authoritative stock row per SKU will inherit the same ~500 completes/s ceiling on this
hardware, no matter how many services sit in front of it. The levers that would move it
are structural — batching commits, sharding hot SKUs, or accepting bounded stock
staleness — and each of them trades away some of the integrity guarantee that this
week gets for free from a single local transaction.
