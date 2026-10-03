# Pipeline self-checkout server

C++20 / Drogon 1.8 / Postgres 18. Same API contract and layers as `layered/`; the windowed
popular-items analytics is now a pipe-and-filter pipeline (`src/analytics/`). Prereqs:
`libdrogon-dev`, `catch2`, `cmake`, `ninja-build`, `docker`, `psql`.

## The pipeline

The system has a 4-stage pipeline:

`POST /transactions/{id}/items` → **window** → **rank** → **format** → **persist** → `popular_snapshots`

| Filter | Purpose |
|---|---|
| **window** | Hopping window over the last 1,000 scans (ring buffer + per-item counts); every 500 scans emits a copy of the counts with `windowStart`/`windowEnd`. |
| **rank** | Picks the top N items by scan count (ties: lower catalog index first). |
| **format** | Maps catalog indexes to sku/name and builds the `/analytics/popular-items` JSON. |
| **persist** | Batches every row waiting in its input pipe and writes them in one `INSERT` (one commit) to Postgres. |

The pipes are unbounded blocking queues (`Pipe<T>`: `std::deque` + mutex + condition variable,
`src/analytics/pipe.hpp`); each filter runs on its own `std::jthread`. The HTTP scan handler only
pushes the item index into the first pipe and never waits on analytics. Shutdown closes the first
pipe; each filter drains its input, closes its output, and exits. `GET /analytics/popular-items`
reads the newest persisted row, unchanged from earlier weeks.

Why persist batches: with one `INSERT` per snapshot, the 100-station stress run fell 12 s behind
(each commit waited ~100 ms behind sale commits while a snapshot arrived every ~92 ms), see
`reports/experiments/unbatched-persist-*.json`. Draining the pipe per write lets the batch grow
with DB latency instead of the backlog growing.

```bash
docker compose up -d --wait                                   # Postgres on :5432
db/reset.sh                                                   # drop + create + seed 2,000 items
cmake --preset release && cmake --build --preset release      # -> build/release/checkout-server
./build/release/checkout-server config.json                   # http://localhost:8080
scripts/run-test.sh [--stations=100 --duration=120]           # reset -> server -> load client -> invariant check
```

Graded runs: `reports/`. Tests: `ctest --preset release -L unit` (no DB) · `ctest --preset release -L db` (needs compose up).
