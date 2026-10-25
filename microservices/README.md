# Microservices self-checkout server

C++20 / Drogon 1.8 / Postgres 18. Same API contract as `services/` (week 4), but each service now owns
its own database and is deployed and restarted on its own. Prereqs: `libdrogon-dev`, `catch2`, `cmake`,
`ninja-build`, `docker`, `psql`.

## Services

| Service | Executable | Port | Public routes (via gateway) | Internal routes | Own database: tables |
|---|---|---|---|---|---|
| **Catalog** | `catalog-service` | 8081 | `GET /items` | — | `catalog`: `items` |
| **Checkout** | `checkout-service` | 8082 | `POST /transactions`, `POST /transactions/{id}/items`, `POST /transactions/{id}/complete`, `GET /transactions/{id}` | — | `checkout`: `sales`, `receipt_lines`, `sale_outbox` |
| **Inventory** | `inventory-service` | 8083 | `GET /inventory/low-stock` | `POST /events/sales` | `inventory`: `stock`, `applied_sales` |
| **Analytics** | `analytics-service` | 8084 | `GET /analytics/popular-items` | `POST /events/scans` | `analytics`: `popular_snapshots` |

**API gateway**: nginx (`gateway/nginx.conf`, compose service `gateway`, container `ms-gateway`) on
:8080 routes public paths by prefix, so the load client still sees one base URL. It does not route
`/events/*`, so the internal routes are reachable only on the service ports.

Each service has its own config file (`config/<service>.json`) naming its own database and the URLs of the
services it calls. No service has another service's database credentials or links another service's DB
code (`CMakeLists.txt`: `db_catalog`, `db_checkout`, `db_inventory`, `db_analytics`, one per service).
For convenience the four databases share one Postgres server (the `checkout-db` container); moving one
to its own server only changes that service's config file.

## How services talk

| From → to | What | How | Guarantee |
|---|---|---|---|
| catalog → everyone | sku / name / price | each service `GET /items` at startup and keeps the catalog in RAM (retries until catalog answers) | read-only reference data |
| checkout → inventory | "sale completed" | transactional outbox + relay thread → `POST /events/sales` | at least once, applied exactly once |
| checkout → analytics | scanned items | in-memory pipe + publisher thread → batched `POST /events/scans` | best effort, retried while analytics is down |

## Completing a sale: an update to two databases

`POST /transactions/{id}/complete` has to record the sale (checkout's database) **and** decrement stock
(inventory's database). The two writes are coordinated by **choreography with a transactional outbox**:

1. **Checkout, one local transaction.** `complete_sale()` (`db/checkout.sql`) inserts the `sales` row, its
   `receipt_lines` and one `sale_outbox` row in a single commit. The sale and the event that announces
   it are written together or not at all. The HTTP response goes back right after this commit.
2. **Relay (thread in checkout).** It reads up to 1,000 pending outbox rows with their lines and POSTs
   them to inventory as one batch. It deletes those outbox rows only after inventory answers 2xx. If
   inventory is down or errors, the rows stay and are resent 200 ms later.
3. **Inventory, one local transaction.** `apply_sales()` (`db/inventory.sql`) inserts each transaction id into
   `applied_sales ... ON CONFLICT DO NOTHING`, then decrements stock (floor at zero, stamps
   `low_stock_since`) only for the ids that were new. A resent batch therefore changes nothing: the relay
   delivers at least once, and the idempotency table makes the effect exactly once.

There is no compensating step because the inventory step cannot be rejected. Stock floors at zero, as
in every earlier week, so a sale never fails for lack of stock. Retries alone get the two databases to
agree. Stock is *eventually* consistent with sales: the low-stock report lags completions by one relay
round-trip (poll interval 10 ms + one POST + one commit).

`scripts/verify.sh` checks the result from outside after each run. It waits for the outbox to drain,
then requires `sales` count = `applied_sales` count, and for every SKU,
`initial − quantity = LEAST(units on checkout's receipts, initial)`.

## Results

| Run | Week 4 (shared DB) | Week 5 (this) | Notes |
|---|---|---|---|
| default, 10 stations / 60 s | 494.7 tx/s, complete p99 101.8 ms | **1,734.8 tx/s**, complete p99 4.8 ms | `reports/report-20261024-213208.json` |
| stress, 100 stations / 120 s | 572.8 tx/s, complete p99 372 ms | **3,204.7 tx/s**, complete p99 24.0 ms | `reports/report-20261024-213420.json` |

Both runs: 0 errors, INVARIANT OK, every sale applied by inventory, and every snapshot persisted
(8,082 = ⌊4,041,145 scans / 500⌋ in the stress run).

Why it got 3–6× faster: in weeks 1–4, every completion locked the stock row of the Zipf-hot
SKU-000001 (in ~67 % of baskets) and held it until the WAL flush, so completions ran one after another.
Now the completion transaction touches only checkout's own tables, which have no shared hot row. Inventory
applies a batch of sales with one UPDATE per SKU, so the hot row is written once per batch instead of
once per sale.

**Independent deployment** (`reports/experiments/report-20261024-213527.json`, 10 stations / 25 s):
inventory was killed at t = 5 s and restarted at t = 14 s. Checkout kept completing sales with 0 errors
while the outbox grew to 16,608 rows. Two seconds after the restart it was down to 9,286, it was
empty before the run ended, and
`verify.sh` reported INVARIANT OK (42,656 sales, 42,656 applied).

## Run it

```bash
docker start checkout-db                                      # Postgres on :5432 (container from earlier weeks)
db/reset.sh                                                   # create (if needed) + reset + seed all four databases
cmake --preset release && cmake --build --preset release      # -> build/release/{catalog,checkout,inventory,analytics}-service
./build/release/checkout-service config/checkout.json         # likewise catalog, inventory, analytics; any start order
docker compose up -d gateway                                  # nginx on :8080 (stop week 4's checkout-gateway first)
scripts/run-test.sh [--stations=100 --duration=120]           # reset -> gateway + 4 services -> load client -> verify.sh
```

Tests: `ctest --preset release -L unit` (no DB) · `ctest --preset release -L db` (needs Postgres) ·
`tests/smoke.sh` (every endpoint through the gateway).
