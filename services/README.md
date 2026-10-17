# Domain-services self-checkout server

C++20 / Drogon 1.8 / Postgres 18. Same API contract as `pipeline/`, split into four domain services.
Each one is its own executable, and all four share one Postgres database. Prereqs: `libdrogon-dev`, `catch2`,
`cmake`, `ninja-build`, `docker`, `psql`.

## Services

| Service | Executable | Port | Routes | Tables it touches |
|---|---|---|---|---|
| **Catalog** | `catalog-service` | 8081 | `GET /items` | `items` (read at startup) |
| **Checkout** | `checkout-service` | 8082 | `POST /transactions`, `POST /transactions/{id}/items`, `POST /transactions/{id}/complete`, `GET /transactions/{id}` | `items` (read), `sales` + `receipt_lines` (write), `stock` (decrement in `complete_sale()`), `scan_events` (append) |
| **Inventory** | `inventory-service` | 8083 | `GET /inventory/low-stock` | `stock` + `items` (read) |
| **Analytics** | `analytics-service` | 8084 | `GET /analytics/popular-items` | `items` (read), `scan_events` (read), `popular_snapshots` (write + read) |

An nginx **gateway** (`gateway/nginx.conf`, compose service `gateway`) listens on :8080 and routes
requests by path prefix, so the load client still sees one base URL. It is a router, not a service.

## No service-to-service calls

The services never call each other. They are coupled only through the database:

- **Checkout → Inventory**: `complete_sale()` decrements `stock` and stamps `low_stock_since`.
  Inventory reads those rows.
- **Checkout → Analytics**: the scan handler pushes the item index into an in-process pipe. One
  publisher thread drains the pipe and writes each batch to `scan_events` in one `INSERT`. The
  analytics service polls `scan_events WHERE id > last` (up to 10,000 rows per read, with a 50 ms
  sleep once it has caught up) and feeds the rows into the same window → rank → format → persist
  pipeline as week 3. Checkout has only one writer thread, so ids commit in order and the poll
  never skips a row.
- **Catalog data**: each service that needs sku/name/price loads its own copy of `items` at startup.

## Sharing the DB access code

`src/db/` is split by table group into one static library per group. Each service links only the
groups it uses, so its link line in `CMakeLists.txt` lists exactly which tables it can touch:

| Library | Source | Linked by |
|---|---|---|
| `db_items` | `db/items.cpp` (`loadCatalog`) | catalog, checkout, analytics |
| `db_sales` | `db/sales.cpp` (`Sales::completeSale`, `findSale`) | checkout |
| `db_stock` | `db/stock.cpp` (`Stock::lowStock`) | inventory |
| `db_scans` | `db/scan_log.cpp` (`ScanLog::append`, `after`) | checkout (append), analytics (after) |
| `db_snapshots` | `db/snapshots.cpp` | analytics |

The schema (`db/schema.sql`, including `complete_sale()`) is shared too. All four services read the
same `config.json` and differ only in port. `src/services/run_service.cpp` handles startup for all of them.

```bash
docker start checkout-db                                      # Postgres on :5432 (container from earlier weeks)
docker compose up -d gateway                                  # nginx on :8080
db/reset.sh                                                   # drop + create + seed 2,000 items
cmake --preset release && cmake --build --preset release      # -> build/release/{catalog,checkout,inventory,analytics}-service
./build/release/checkout-service config.json 8082             # likewise catalog 8081, inventory 8083, analytics 8084
scripts/run-test.sh [--stations=100 --duration=120]           # reset -> gateway + 4 services -> load client -> invariant check
```

`docker compose up` without a service name also tries to create `db`, and that fails if the
`checkout-db` container already exists from an earlier week. Start that container with `docker start` instead.

Graded runs: `reports/`. Tests: `ctest --preset release -L unit` (no DB) · `ctest --preset release -L db` (needs Postgres).
