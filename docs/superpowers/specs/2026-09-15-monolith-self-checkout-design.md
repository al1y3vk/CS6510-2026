# Monolith Self-Checkout Server — Design Spec

Date: 2026-09-15
Status: approved in conversation, awaiting written-spec review
Scope: Week 1 (monolith) implementation of `spec/self-checkout-openapi.yaml`

---

## 1. Goal

One C++ process (Drogon) plus one Postgres container that passes the unmodified
`load-client` in default mode (10 stations, 60 s) and stress mode
(`--stations=100 --duration=120`), keeps the stock invariant under concurrency,
and can be reset to a clean state with one command between runs.

Success criteria:

1. `scripts/run-test.sh` (default) and `scripts/run-test.sh --stations=100 --duration=120`
   both finish with 0 client errors and `INVARIANT OK`.
2. `ctest -L unit` passes in < 1 s; `ctest -L db` passes against the compose DB.
3. `monolith/reports/` holds the two JSON reports; `monolith/ANALYSIS.md` exists.

---

## 2. Decisions already made

| Area | Decision | Why |
| --- | --- | --- |
| Language / HTTP | C++20, Drogon 1.8.7 (apt `libdrogon-dev`) | Async framework with routing, filters (week 8 rate limiter), HTTP client (weeks 4–5), and an async Postgres client with coroutines. |
| Database | Postgres 16 in Docker Compose | Networked and multi-writer, so it survives the service-based / microservices weeks unchanged. |
| Open baskets | RAM only; persisted at completion | Scan touches no DB. Trade-off: a crash loses open baskets, never completed sales. |
| Out-of-stock at completion | Charge the customer, floor stock at 0 | Spec: never negative, never gate a scan, payment always succeeds. Matches the mock. |
| Money | Integer cents in RAM and DB; double only in JSON | No float drift after 20 scans. |
| Popular items | Hopping window in RAM; top-10 snapshot persisted every 500 scans | Spec requires DB persistence of the ranking; scans never wait on that insert. |
| JSON | jsoncpp (bundled with Drogon) | Nothing to vendor. |
| Tests | Catch2 v3 (apt, installed) | One header, terse assertions. |
| Names | tables `sales`, `receipt_lines`; RAM module `Transactions` | User preference. |

---

## 3. Repository layout

```
CS6510-2026/
├── spec/                      (given) OpenAPI contract
├── load-client/               (given) never edited
├── mockserver/                (given) reference only
├── docs/superpowers/specs/    this file
└── monolith/
    ├── CMakeLists.txt
    ├── CMakePresets.json
    ├── config.json            Drogon + app config (one file)
    ├── docker-compose.yml     Postgres 16
    ├── README.md              5-line how-to-run
    ├── ANALYSIS.md            architectural characteristics (graded)
    ├── reports/               report-*.json from the load client (graded)
    ├── db/
    │   ├── schema.sql         drop + create + seed + SQL functions
    │   ├── reset.sh           runs schema.sql through psql
    │   └── verify.sql         stock invariant check; 0 rows = pass
    ├── scripts/
    │   └── run-test.sh        reset → server → client → verify
    ├── src/
    │   ├── main.cpp
    │   ├── app_config.hpp/.cpp        reads custom_config into a struct
    │   ├── money.hpp/.cpp             cents↔double, rfc3339(), pgArray()
    │   ├── catalog.hpp/.cpp           read-only item table
    │   ├── transactions.hpp/.cpp      open baskets (RAM)
    │   ├── popularity_window.hpp/.cpp hopping window (RAM)
    │   ├── inventory.hpp/.cpp         SQL: complete_sale, low stock, find sale
    │   ├── snapshots.hpp/.cpp         SQL: insert / read latest popular snapshot
    │   ├── api_error.hpp              apiError(status, code, message)
    │   └── controllers/
    │       ├── catalog_controller.hpp/.cpp
    │       ├── transactions_controller.hpp/.cpp
    │       ├── inventory_controller.hpp/.cpp
    │       └── analytics_controller.hpp/.cpp
    └── tests/
        ├── CMakeLists.txt
        ├── unit/
        │   ├── transactions_test.cpp
        │   ├── popularity_window_test.cpp
        │   └── money_test.cpp
        └── db/
            └── inventory_test.cpp
```

Two static libraries keep the dependency direction clean:

- `checkout_core` — catalog, transactions, popularity_window, money, app_config.
  **No Drogon dependency.** Unit tests link only this.
- `checkout_db` — inventory, snapshots. Depends on core + Drogon (for the DB client).

Controllers and `main.cpp` sit on top of both.

---

## 4. Runtime architecture

```
load-client ──HTTP/1.1 keep-alive──▶ Drogon (IO threads = CPU cores)
                                        │
                                        ├─ controllers/*      parse JSON → call module → build JSON
                                        │
                                        ├─ Catalog            read-only after startup, no lock
                                        ├─ Transactions       open baskets, shared_mutex map + per-basket mutex
                                        ├─ PopularityWindow   ring buffer + counts, one mutex
                                        ├─ Inventory          SQL only (co_await)
                                        └─ Snapshots          SQL only (fire-and-forget insert, co_await read)
                                                  │
                                                  ▼  Drogon DbClient, 16 connections
                                              Postgres 16 (docker compose)
```

| Request | RAM | DB |
| --- | --- | --- |
| `GET /items` | catalog | – |
| `POST /transactions` | create basket | – |
| `POST /transactions/{id}/items` | append to basket, push into window | every 500th scan: insert snapshot (async, not awaited) |
| `POST /transactions/{id}/complete` | mark basket COMPLETING, then remove | one call to `complete_sale()` (one round-trip, one DB transaction) |
| `GET /transactions/{id}` | basket if open | `sales` row if completed |
| `GET /inventory/low-stock` | – | select from `stock` |
| `GET /analytics/popular-items` | – | select newest `popular_snapshots` row |

Every handler is a coroutine (`drogon::Task<HttpResponsePtr>`). Only the rows
with a DB entry ever `co_await`. Nothing blocks an IO thread.

---

## 5. Database

### 5.1 `monolith/docker-compose.yml`

```yaml
services:
  db:
    image: postgres:16
    container_name: checkout-db
    environment:
      POSTGRES_USER: checkout
      POSTGRES_PASSWORD: checkout
      POSTGRES_DB: checkout
    ports:
      - "5432:5432"
    volumes:
      - pgdata:/var/lib/postgresql/data
    healthcheck:
      test: ["CMD-SHELL", "pg_isready -U checkout -d checkout"]
      interval: 2s
      timeout: 2s
      retries: 15

volumes:
  pgdata:
```

What each part does:

- `image: postgres:16` — pulled from Docker Hub on first `up`. The image's
  entrypoint script reads the three `POSTGRES_*` env vars **only the first time
  it starts on an empty volume** and creates that role + database. Changing them
  later does nothing until you `docker compose down -v`.
- `ports: "5432:5432"` — host port 5432 → container port 5432. This is why the
  server and host `psql` both connect to `localhost:5432`.
- `volumes: pgdata` — a named volume; data survives `docker compose down` and
  laptop reboots. We reset with SQL, not by deleting the volume.
- `healthcheck` — `docker compose up -d --wait` blocks until `pg_isready`
  succeeds, so scripts never race the DB startup.

### 5.2 Docker / psql cheat sheet

```bash
cd monolith
docker compose up -d --wait        # first time: pulls image (~150 MB), creates volume + container
docker compose ps                  # STATUS should say "healthy"
docker compose logs -f db          # Ctrl+C to stop following
psql postgresql://checkout:checkout@localhost:5432/checkout   # host psql → container
#   \dt            list tables          \d stock      describe a table
#   \x             toggle wide output   \q            quit
docker compose exec db psql -U checkout -d checkout          # same shell, from inside the container
docker compose stop                # stop, keep container + data
docker compose down                # remove container, keep data volume
docker compose down -v             # remove container AND volume = factory reset
```

Connection URL used everywhere (env var `PGURL`, default below):

```
postgresql://checkout:checkout@localhost:5432/checkout
```

### 5.3 `monolith/db/schema.sql`

Run with psql variables `catalog_size` and `stock_per_item` (see reset.sh).

```sql
-- Idempotent: drop everything we own, recreate, seed.
DROP TABLE IF EXISTS receipt_lines, sales, popular_snapshots, stock, items CASCADE;
DROP FUNCTION IF EXISTS complete_sale(TEXT, TEXT, TIMESTAMPTZ, TEXT[], INT[], INT[], INT, BIGINT, INT);
DROP FUNCTION IF EXISTS iso8601(TIMESTAMPTZ);

CREATE TABLE items (
    sku         TEXT PRIMARY KEY,
    id          INT  NOT NULL UNIQUE,          -- catalog order = Zipf rank (1-based)
    name        TEXT NOT NULL,
    price_cents INT  NOT NULL CHECK (price_cents > 0)
);

CREATE TABLE stock (
    sku              TEXT PRIMARY KEY REFERENCES items(sku),
    quantity         INT  NOT NULL CHECK (quantity >= 0),
    initial_quantity INT  NOT NULL,
    low_stock_since  TIMESTAMPTZ                -- set once, first time quantity < threshold
);

CREATE TABLE sales (
    transaction_id TEXT PRIMARY KEY,
    station_id     TEXT        NOT NULL,
    started_at     TIMESTAMPTZ NOT NULL,
    completed_at   TIMESTAMPTZ NOT NULL,
    item_count     INT         NOT NULL,
    total_cents    BIGINT      NOT NULL
);

CREATE TABLE receipt_lines (
    transaction_id   TEXT NOT NULL REFERENCES sales(transaction_id),
    sku              TEXT NOT NULL REFERENCES items(sku),
    quantity         INT  NOT NULL CHECK (quantity > 0),
    unit_price_cents INT  NOT NULL,
    PRIMARY KEY (transaction_id, sku)
);

CREATE TABLE popular_snapshots (
    id           BIGSERIAL PRIMARY KEY,
    window_start BIGINT      NOT NULL,
    window_end   BIGINT      NOT NULL,
    computed_at  TIMESTAMPTZ NOT NULL,
    items        JSONB       NOT NULL           -- already in response shape, see §6.5
);

-- Timestamps leave Postgres already formatted; C++ never parses dates.
CREATE FUNCTION iso8601(ts TIMESTAMPTZ) RETURNS TEXT LANGUAGE sql IMMUTABLE AS $$
    SELECT to_char(ts AT TIME ZONE 'UTC', 'YYYY-MM-DD"T"HH24:MI:SS.MS"Z"')
$$;

-- The whole completion in one statement = one implicit transaction = one round-trip.
-- Caller passes arrays sorted by SKU with no duplicates.
CREATE FUNCTION complete_sale(
    p_tx_id       TEXT,
    p_station_id  TEXT,
    p_started_at  TIMESTAMPTZ,
    p_skus        TEXT[],
    p_qtys        INT[],
    p_unit_cents  INT[],
    p_item_count  INT,
    p_total_cents BIGINT,
    p_threshold   INT
) RETURNS TIMESTAMPTZ LANGUAGE plpgsql AS $$
DECLARE
    v_now TIMESTAMPTZ := clock_timestamp();
BEGIN
    -- Lock rows in a fixed order so two overlapping baskets can never deadlock.
    PERFORM 1 FROM stock WHERE sku = ANY(p_skus) ORDER BY sku FOR UPDATE;

    UPDATE stock s
       SET quantity        = GREATEST(0, s.quantity - b.qty),
           low_stock_since = CASE
                                 WHEN s.low_stock_since IS NULL
                                  AND GREATEST(0, s.quantity - b.qty) < p_threshold
                                 THEN v_now
                                 ELSE s.low_stock_since
                             END
      FROM unnest(p_skus, p_qtys) AS b(sku, qty)
     WHERE s.sku = b.sku;

    INSERT INTO sales (transaction_id, station_id, started_at, completed_at, item_count, total_cents)
    VALUES (p_tx_id, p_station_id, p_started_at, v_now, p_item_count, p_total_cents);

    INSERT INTO receipt_lines (transaction_id, sku, quantity, unit_price_cents)
    SELECT p_tx_id, b.sku, b.qty, b.cents
      FROM unnest(p_skus, p_qtys, p_unit_cents) AS b(sku, qty, cents);

    RETURN v_now;
END
$$;

-- Seed. Same SKU format, names, and price formula as the mock:
--   price = 0.5 + (i % 47) * 0.35  →  cents = 50 + (i % 47) * 35
INSERT INTO items (sku, id, name, price_cents)
SELECT format('SKU-%s', lpad(i::text, 6, '0')), i, 'Item ' || i, 50 + (i % 47) * 35
  FROM generate_series(1, :catalog_size) AS i;

INSERT INTO stock (sku, quantity, initial_quantity)
SELECT sku, :stock_per_item, :stock_per_item FROM items;
```

Why a SQL function instead of BEGIN/…/COMMIT from C++: Drogon's
`Transaction` object commits when it is destroyed and has no awaitable commit,
so "send 200 only after COMMIT" would need a hand-written awaiter. A function
call is one statement: when `execSqlCoro` returns, the transaction is committed.
It is also one round-trip instead of five. Trade-off (for ANALYSIS.md): inventory
logic lives in the DB; in the service-based week it moves into the inventory
service.

### 5.4 `monolith/db/reset.sh`

```bash
#!/usr/bin/env bash
# Drop + recreate + seed. Run before every test run.
#   CATALOG_SIZE=5 STOCK_PER_ITEM=3 ./reset.sh   (tiny DB for the db tests)
set -euo pipefail
cd "$(dirname "$0")"
: "${PGURL:=postgresql://checkout:checkout@localhost:5432/checkout}"
: "${CATALOG_SIZE:=2000}"
: "${STOCK_PER_ITEM:=10000}"
psql "$PGURL" -q -v ON_ERROR_STOP=1 \
     -v catalog_size="$CATALOG_SIZE" -v stock_per_item="$STOCK_PER_ITEM" \
     -f schema.sql
echo "db reset: $CATALOG_SIZE items × $STOCK_PER_ITEM stock"
```

### 5.5 `monolith/db/verify.sql`

```sql
-- One row per SKU whose stock movement disagrees with its receipt lines.
-- Zero rows = invariant holds. Floor-at-zero means decremented = LEAST(sold, initial).
SELECT s.sku,
       s.initial_quantity,
       s.quantity                              AS final_quantity,
       COALESCE(sold.units, 0)                 AS units_sold,
       s.initial_quantity - s.quantity         AS decremented
  FROM stock s
  LEFT JOIN (SELECT sku, SUM(quantity) AS units FROM receipt_lines GROUP BY sku) sold USING (sku)
 WHERE s.quantity < 0
    OR s.initial_quantity - s.quantity <> LEAST(COALESCE(sold.units, 0), s.initial_quantity)
 ORDER BY s.sku;
```

### 5.6 Other queries (used by `inventory.cpp` / `snapshots.cpp`)

```sql
-- Catalog load at startup
SELECT sku, id, name, price_cents FROM items ORDER BY id;

-- Low stock (threshold = query override or configured default)
SELECT s.sku, i.name, s.quantity, iso8601(s.low_stock_since) AS since
  FROM stock s JOIN items i USING (sku)
 WHERE s.quantity < $1
 ORDER BY s.quantity, s.sku;

-- Completed-transaction lookup (409 vs 404, and GET /transactions/{id})
SELECT station_id, iso8601(started_at) AS started_at, item_count, total_cents
  FROM sales WHERE transaction_id = $1;

-- Snapshot insert (fire-and-forget)
INSERT INTO popular_snapshots (window_start, window_end, computed_at, items)
VALUES ($1, $2, $3::timestamptz, $4::jsonb);

-- Latest snapshot
SELECT window_start, window_end, iso8601(computed_at) AS computed_at, items::text
  FROM popular_snapshots ORDER BY id DESC LIMIT 1;
```

Performance knobs deliberately left at Postgres defaults for the graded runs
(candidates for ANALYSIS.md experiments): `synchronous_commit = off` (faster
commits, loses the last few ms of sales on a DB crash), `is_fast: true` DB
client (per-IO-thread connections, no mutex).

---

## 6. In-process modules

All modules take plain values; none of the core modules includes a Drogon header.

### 6.1 `money.hpp`

```cpp
inline double centsToDouble(int64_t cents) { return cents / 100.0; }
// "2026-09-15T04:12:33.123Z" — UTC, millisecond precision.
std::string rfc3339(std::chrono::system_clock::time_point);
// {a,b,c} — Postgres array literal. SKUs and ints need no quoting.
std::string pgArray(const std::vector<std::string>&);
std::string pgArray(const std::vector<int64_t>&);
```

`pgArray` exists because Drogon 1.8.7 cannot bind `std::vector` as a Postgres
array parameter; the literal is bound as text and cast in SQL (`$4::text[]`).

### 6.2 `catalog.hpp`

```cpp
struct Item {
    uint32_t    index;        // 0-based position = id - 1
    std::string sku;
    std::string name;
    int64_t     price_cents;
};

class Catalog {
public:
    explicit Catalog(std::vector<Item> items);      // in id order, index == position
    const Item&  at(uint32_t index) const;
    const Item*  find(std::string_view sku) const;  // nullptr if unknown
    size_t       size() const;
    const std::vector<Item>& all() const;
private:
    std::vector<Item> items_;
    std::unordered_map<std::string, uint32_t> bySku_;
};
```

Loaded once at startup, never written again, so no lock. Everything else stores
`uint32_t` indices, not SKU strings.

### 6.3 `transactions.hpp`

```cpp
enum class BasketState { Open, Completing };

struct Basket {
    std::string id;
    std::string stationId;
    std::chrono::system_clock::time_point startedAt;
    std::mutex  mu;                       // guards the fields below
    BasketState state = BasketState::Open;
    std::vector<uint32_t> itemIndices;    // one entry per scanned unit, scan order
    int64_t runningTotalCents = 0;
};

class Transactions {
public:
    std::shared_ptr<Basket> start(std::string stationId);     // id = "tx-" + uuid
    std::shared_ptr<Basket> find(const std::string& id) const; // nullptr if not open
    void                    remove(const std::string& id);
    size_t                  openCount() const;                 // for tests / logging
private:
    mutable std::shared_mutex mu_;
    std::unordered_map<std::string, std::shared_ptr<Basket>> open_;
};
```

Locking rules:

- `mu_` (map) is held only for the hash lookup / insert / erase. Readers
  (`find`) take it shared; `start` and `remove` take it exclusive.
- `Basket::mu` is held while reading or writing the basket's fields. Only one
  station ever touches a given basket, so it is uncontended.
- Never hold both at once.

State machine per basket: `Open --complete--> Completing --DB ok--> removed`,
`Completing --DB error--> Open` (client may retry). Scan while `Completing`
→ 409 `TRANSACTION_NOT_OPEN`.

### 6.4 `popularity_window.hpp`

```cpp
struct PopularEntry { uint32_t index; uint32_t count; };

struct Snapshot {
    uint64_t windowStart;                 // = max(1, seq - windowSize + 1)
    uint64_t windowEnd;                   // = seq
    std::chrono::system_clock::time_point computedAt;
    std::vector<PopularEntry> top;        // count desc, then index asc; size <= topN
};

class PopularityWindow {
public:
    PopularityWindow(size_t windowSize, size_t slideInterval, size_t topN, size_t catalogSize);
    // Records one scan. Returns a snapshot iff seq % slideInterval == 0.
    std::optional<Snapshot> record(uint32_t itemIndex);
private:
    std::mutex mu_;
    uint64_t seq_ = 0;                    // global scan number, 1-based after first record
    std::vector<uint32_t> ring_;          // windowSize slots; UINT32_MAX = empty
    std::vector<uint32_t> counts_;        // catalogSize entries
    // config copies
};
```

`record`, under `mu_`: `++seq_`; slot = `(seq_ - 1) % windowSize`; if the slot
holds an item, `--counts_[old]`; write new index; `++counts_[new]`. If
`seq_ % slideInterval == 0`, build `top` with `std::partial_sort` over indices
with `count > 0` (~10 µs for 2,000 items), return it. Roughly 50 ns otherwise.

The window does no I/O. The scan handler owns "if a snapshot came back, insert
it". That keeps the window unit-testable without a database.

### 6.5 `inventory.hpp` (DB, `checkout_db`)

```cpp
struct SaleLine { std::string sku; int32_t quantity; int64_t unitPriceCents; };

struct SaleRow {          // from `sales`
    std::string stationId;
    std::string startedAt; // already iso8601
    int32_t     itemCount;
    int64_t     totalCents;
};

struct LowStockRow { std::string sku; std::string name; int32_t quantity; std::optional<std::string> since; };

class Inventory {
public:
    Inventory(drogon::orm::DbClientPtr db, int lowStockThreshold);
    // lines sorted by sku, no duplicates. Returns completed_at (iso8601). Throws DrogonDbException.
    drogon::Task<std::string> completeSale(const Basket& b, const std::vector<SaleLine>& lines,
                                           int32_t itemCount, int64_t totalCents);
    drogon::Task<std::optional<SaleRow>>   findSale(std::string txId);
    drogon::Task<std::vector<LowStockRow>> lowStock(int threshold);
};
```

`completeSale` issues exactly one statement:

```sql
SELECT iso8601(complete_sale($1, $2, $3::timestamptz, $4::text[], $5::int[], $6::int[], $7::int, $8::bigint, $9::int))
```

with `$4..$6` built by `pgArray`.

### 6.6 `snapshots.hpp` (DB, `checkout_db`)

```cpp
struct StoredSnapshot { uint64_t windowStart, windowEnd; std::string computedAt; Json::Value items; };

class Snapshots {
public:
    explicit Snapshots(drogon::orm::DbClientPtr db);
    // Fire-and-forget: uses execSqlAsync; on error logs and drops. Never awaited by a scan.
    void insertAsync(const Snapshot& s, const Catalog& c);
    drogon::Task<std::optional<StoredSnapshot>> latest();
};
```

`items` JSONB is stored already in response shape so the GET only slices it:

```json
[{"sku":"SKU-000001","name":"Item 1","scanCount":143,"rank":1}, ...]
```

---

## 7. HTTP layer

### 7.1 Controllers

One `drogon::HttpController` per spec tag. Coroutine handlers take parameters
**by value** (the coroutine frame outlives the caller).

```cpp
class TransactionsController : public drogon::HttpController<TransactionsController> {
public:
    METHOD_LIST_BEGIN
    ADD_METHOD_TO(TransactionsController::start,    "/transactions",               drogon::Post);
    ADD_METHOD_TO(TransactionsController::scan,     "/transactions/{1}/items",     drogon::Post);
    ADD_METHOD_TO(TransactionsController::complete, "/transactions/{1}/complete",  drogon::Post);
    ADD_METHOD_TO(TransactionsController::status,   "/transactions/{1}",           drogon::Get);
    METHOD_LIST_END

    drogon::Task<drogon::HttpResponsePtr> start(drogon::HttpRequestPtr req);
    drogon::Task<drogon::HttpResponsePtr> scan(drogon::HttpRequestPtr req, std::string txId);
    drogon::Task<drogon::HttpResponsePtr> complete(drogon::HttpRequestPtr req, std::string txId);
    drogon::Task<drogon::HttpResponsePtr> status(drogon::HttpRequestPtr req, std::string txId);
};
```

Controllers reach the modules through one `struct Services { Catalog&; Transactions&;
PopularityWindow&; Inventory&; Snapshots&; AppConfig& }` set up in `main.cpp`
(a static pointer assigned inside the beginning advice in §9, before the listener
opens; Drogon constructs controllers itself, so no constructor injection).

### 7.2 Handler flows

**POST /transactions**
1. `req->getJsonObject()`; null, or `stationId` missing / not a non-empty string → 400 `INVALID_REQUEST`.
2. `basket = transactions.start(stationId)`.
3. 201 `Transaction{transactionId, stationId, status:"OPEN", itemCount:0, runningTotal:0.0, startedAt}`.

**POST /transactions/{id}/items**
1. Body `sku` missing / empty → 400 `INVALID_REQUEST`.
2. `basket = transactions.find(id)`; if null: `co_await inventory.findSale(id)` → found: 409 `TRANSACTION_NOT_OPEN`, else 404 `NOT_FOUND`.
3. `item = catalog.find(sku)`; null → 404 `UNKNOWN_SKU`.
4. Lock `basket->mu`: state ≠ Open → 409 `TRANSACTION_NOT_OPEN`. Else push index, add price, copy `itemCount` and `runningTotalCents`. Unlock.
5. `snap = window.record(item->index)`; if set → `snapshots.insertAsync(*snap, catalog)`.
6. 200 `ScanResult{transactionId, sku, name, unitPrice, itemCount, runningTotal}`.

**POST /transactions/{id}/complete**
1. Find basket as in scan step 2.
2. Lock: state ≠ Open → 409 `TRANSACTION_NOT_OPEN`; empty → 409 `EMPTY_BASKET`; set `Completing`; copy `itemIndices`, `runningTotalCents`. Unlock.
3. Aggregate indices → `(index, qty)` in first-scan order (for the receipt); build a SKU-sorted copy as `SaleLine`s (for the DB).
4. `completedAt = co_await inventory.completeSale(...)`. On `DrogonDbException`: lock, state = Open, unlock; log; 500 `INTERNAL_ERROR`.
5. `transactions.remove(id)`.
6. 200 `Receipt{transactionId, stationId, itemCount, totalAmount, startedAt, completedAt, lines[]}` — lines in first-scan order.

**GET /transactions/{id}**
1. Open basket → `Transaction` with status `OPEN` (a `Completing` basket still reports `OPEN`; it is `COMPLETED` only after commit).
2. Else `findSale` → status `COMPLETED`, `itemCount`, `runningTotal = total_cents/100`.
3. Else 404 `NOT_FOUND`.

**GET /inventory/low-stock**
1. `threshold` = `req->getOptionalParameter<int>("threshold")`; present but < 0 or unparsable → 400 `INVALID_REQUEST`; absent → configured default.
2. `rows = co_await inventory.lowStock(threshold)`; `generatedAt = rfc3339(now)`.
3. 200 `{threshold, generatedAt, alerts:[{sku, name, currentStock, threshold, triggeredAt: since ?? generatedAt}]}`.

**GET /analytics/popular-items**
1. `limit` = query param, default 10, clamped to `[1, topN]`.
2. `snap = co_await snapshots.latest()`.
3. None → `{windowSize, slideInterval, windowStart:0, windowEnd:0, computedAt: now, items:[]}`.
4. Else same envelope with stored values and `items` sliced to `limit`.

**GET /items** — `{items:[{sku, name, price}]}` straight from `catalog.all()`.

### 7.3 Errors

```cpp
// api_error.hpp
drogon::HttpResponsePtr apiError(drogon::HttpStatusCode, std::string_view code, std::string_view message);
```

| HTTP | `error` | When |
| --- | --- | --- |
| 400 | `INVALID_REQUEST` | body not JSON, missing/empty `stationId` or `sku`, bad `threshold`/`limit` |
| 404 | `NOT_FOUND` | unknown transaction id (not open, not in `sales`) or unknown route |
| 404 | `UNKNOWN_SKU` | SKU not in catalog |
| 409 | `TRANSACTION_NOT_OPEN` | id found in `sales`, or basket is `Completing` |
| 409 | `EMPTY_BASKET` | complete with zero scans |
| 500 | `INTERNAL_ERROR` | any `DrogonDbException`; always logged with the SQL error text |

Same codes as the mock, so the client sees identical bodies.

### 7.4 JSON, numbers, IDs, time

- Responses are built as `Json::Value` and returned via
  `HttpResponse::newHttpJsonResponse`. Drogon's `float_precision_in_json`
  (§8) is `2 decimal`, so `185` cents serializes as `1.85`.
- Transaction IDs: `"tx-" + drogon::utils::getUuid()` (32 hex chars). Never
  collide across server restarts, so a stale `sales` table cannot break a run.
- Timestamps generated in C++ (`startedAt`, `generatedAt`, `computedAt` for the
  empty case) use `rfc3339()`. Timestamps read from Postgres come back
  pre-formatted by `iso8601()`. C++ never parses a date string.

---

## 8. Config — `monolith/config.json`

```json
{
  "listeners": [{ "address": "0.0.0.0", "port": 8080 }],
  "app": {
    "number_of_threads": 0,
    "float_precision_in_json": { "precision": 2, "precision_type": "decimal" },
    "log": { "log_path": "", "log_level": "INFO" }
  },
  "db_clients": [{
    "name": "default",
    "rdbms": "postgresql",
    "host": "127.0.0.1",
    "port": 5432,
    "dbname": "checkout",
    "user": "checkout",
    "passwd": "checkout",
    "is_fast": false,
    "connection_number": 16
  }],
  "custom_config": {
    "low_stock_threshold": 50,
    "window_size": 1000,
    "slide_interval": 500,
    "popular_top_n": 10
  }
}
```

- `number_of_threads: 0` = one IO thread per CPU core.
- `is_fast: false` = one shared, mutex-guarded client usable from any thread
  (including the startup catalog load). Flip to `true` as a later experiment.
- `custom_config` is read once into `AppConfig` via `app().getCustomConfig()`.

```cpp
struct AppConfig { int lowStockThreshold; size_t windowSize, slideInterval, popularTopN; };
AppConfig loadAppConfig(const Json::Value& custom);   // throws on missing/invalid key
```

Usage: `checkout-server [path/to/config.json]`, default `./config.json`.

---

## 9. Startup sequence (`main.cpp`)

```cpp
int main(int argc, char** argv) {
    auto& app = drogon::app();
    app.loadConfigFile(argc > 1 ? argv[1] : "config.json");
    app.registerBeginningAdvice([] {
        // Runs on the main loop after DB clients exist, before the listener opens.
        auto db  = drogon::app().getDbClient("default");
        auto cfg = loadAppConfig(drogon::app().getCustomConfig());
        auto rows = db->execSqlSync("SELECT sku, id, name, price_cents FROM items ORDER BY id");
        if (rows.empty()) { LOG_ERROR << "catalog is empty — run monolith/db/reset.sh"; std::exit(1); }
        // build Catalog, Transactions, PopularityWindow(cfg..., catalog.size()), Inventory, Snapshots
        // publish them through the Services pointer the controllers read
        LOG_INFO << "catalog loaded: " << rows.size() << " items";
    });
    app.run();
}
```

`execSqlSync` is safe here: the non-fast DB client runs its connections on its
own threads, so blocking the main loop while waiting for the result is fine.

---

## 10. Build

`CMakeLists.txt` (outline):

```cmake
cmake_minimum_required(VERSION 3.28)
project(checkout LANGUAGES CXX)
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)         # clangd reads build/<preset>/compile_commands.json

find_package(Drogon CONFIG REQUIRED)

add_library(checkout_core STATIC
    src/money.cpp src/catalog.cpp src/transactions.cpp src/popularity_window.cpp src/app_config.cpp)
target_include_directories(checkout_core PUBLIC src)
target_link_libraries(checkout_core PUBLIC Jsoncpp_lib)     # app_config reads Json::Value

add_library(checkout_db STATIC src/inventory.cpp src/snapshots.cpp)
target_link_libraries(checkout_db PUBLIC checkout_core Drogon::Drogon)

add_executable(checkout-server
    src/main.cpp
    src/controllers/catalog_controller.cpp
    src/controllers/transactions_controller.cpp
    src/controllers/inventory_controller.cpp
    src/controllers/analytics_controller.cpp)
target_link_libraries(checkout-server PRIVATE checkout_db Drogon::Drogon)

enable_testing()
add_subdirectory(tests)
```

`CMakePresets.json`: two configure presets, `debug` and `release`, both Ninja,
binary dirs `build/debug` and `build/release`; matching build and test presets.
Graded runs use `release`. Daily loop:

```bash
cmake --preset release && cmake --build --preset release
./build/release/checkout-server config.json
```

A symlink `compile_commands.json -> build/debug/compile_commands.json` in
`monolith/` keeps clangd happy. Add `monolith/build/` and that symlink to the
repo `.gitignore`.

---

## 11. Tests

`tests/CMakeLists.txt` builds two binaries:

| Target | Links | ctest label | Needs |
| --- | --- | --- | --- |
| `checkout-unit-tests` | `checkout_core`, `Catch2::Catch2WithMain` | `unit` | nothing |
| `checkout-db-tests` | `checkout_db`, `Catch2::Catch2WithMain` | `db` | compose DB up |

### 11.1 Unit (`ctest -L unit`, < 1 s)

`transactions_test.cpp`
- `start` returns a basket with unique `tx-` id, given station, `Open`, empty.
- `find` of unknown id → `nullptr`; after `remove`, `find` → `nullptr`.
- Two `start`s never share an id (loop 10,000, check set size).

`popularity_window_test.cpp` (window 1000, slide 500, topN 10 unless stated)
- `record` × 499 → no snapshot; the 500th → `windowStart 1, windowEnd 500`.
- 1000th → `1..1000`; 1500th → `501..1500`.
- Item scanned only in scans 1–500 has count 0 in the 1500 snapshot (eviction).
- Ordering: counts desc, ties by index asc; `top.size() <= topN`; fewer distinct items than topN → shorter list.
- Small window (size 4, slide 2, catalog 3) end-to-end sequence with hand-computed counts.

`money_test.cpp`
- `centsToDouble(185) == 1.85`, `centsToDouble(0) == 0.0`.
- `pgArray({"SKU-000001","SKU-000002"}) == "{SKU-000001,SKU-000002}"`, empty → `"{}"`.
- `rfc3339(epoch + 1,700,000,000,123 ms) == "2023-11-14T22:13:20.123Z"`.

### 11.2 DB (`ctest -L db`)

Uses `drogon::orm::DbClient::newPgClient(PGURL, 2)` + `execSqlSync`; no
Drogon app. Fixture runs `CATALOG_SIZE=5 STOCK_PER_ITEM=3 db/reset.sh` via
`std::system` before each case; the path comes from
`target_compile_definitions(checkout-db-tests PRIVATE MONOLITH_DIR="${CMAKE_SOURCE_DIR}")`.

`inventory_test.cpp`
- `completeSale` of {SKU-1×2, SKU-3×1}: stock 3→1 and 3→2; one `sales` row with
  `item_count 3`; two `receipt_lines`; returns an iso8601 string.
- Floor at zero: sell 5 of SKU-2 (stock 3) → quantity 0; receipt line quantity 5.
- `low_stock_since` (threshold 2): first sale crossing sets it; second sale of the
  same SKU leaves it unchanged.
- `findSale` returns the row after completion, `nullopt` before.
- `lowStock(3)` after the above lists exactly the SKUs with quantity < 3, with names.
- `verify.sql` returns 0 rows after all of the above.

### 11.3 End-to-end (`scripts/run-test.sh`)

The real load client. Passes when the client reports 0 errors and the script
prints `INVARIANT OK`. Run once per graded configuration.

---

## 12. `monolith/scripts/run-test.sh`

```bash
#!/usr/bin/env bash
# usage: scripts/run-test.sh [load-client flags...]
#   scripts/run-test.sh                                 # default 10 stations, 60 s
#   scripts/run-test.sh --stations=100 --duration=120   # stress
set -euo pipefail
ROOT=$(cd "$(dirname "$0")/.." && pwd)
: "${PGURL:=postgresql://checkout:checkout@localhost:5432/checkout}"

"$ROOT/db/reset.sh"
"$ROOT/build/release/checkout-server" "$ROOT/config.json" &
SERVER=$!
trap 'kill "$SERVER" 2>/dev/null || true' EXIT
until curl -sf http://localhost:8080/items >/dev/null; do sleep 0.2; done

(cd "$ROOT/../load-client" && ./run.sh --reportDir="$ROOT/reports" "$@")

MISMATCHES=$(psql "$PGURL" -tA -f "$ROOT/db/verify.sql" | wc -l)
if [ "$MISMATCHES" -eq 0 ]; then
    echo "INVARIANT OK (0 mismatches)"
else
    echo "INVARIANT VIOLATED ($MISMATCHES SKUs):"
    psql "$PGURL" -f "$ROOT/db/verify.sql"
    exit 1
fi
```

---

## 13. Deliverables

- `monolith/reports/report-*.json` — one from the default run, one from
  `--stations=100 --duration=120`. Do not hand-edit.
- `monolith/ANALYSIS.md` — sections:
  1. Architectural characteristics with concrete requirements (performance,
     scalability, data integrity/correctness, reliability, testability,
     deployability, simplicity). Example: "performance: p99 scan < 5 ms at
     10 stations on the dev laptop".
  2. Top 3 prioritized: performance, data integrity, testability (or as the
     measured results argue). Explicit trade-offs: baskets in RAM (perf vs
     reliability), floor-at-zero (spec constraints vs strict accounting),
     completion logic in a SQL function (perf + correctness vs portability /
     logic-in-DB), fire-and-forget snapshot (perf vs a possible missing
     snapshot on crash).
  3. Results table for both runs (p50/p95/p99 per op, throughput, error count)
     plus `INVARIANT OK` evidence.
- `monolith/README.md` — prerequisites, `docker compose up -d --wait`,
  `db/reset.sh`, build, `scripts/run-test.sh`.

---

## 14. Implementation order (with estimates)

Time is for the person writing it, first time with Drogon.

| # | Step | Verify | Est. |
| --- | --- | --- | --- |
| 1 | `docker-compose.yml`, `db/schema.sql`, `reset.sh`, `verify.sql` | `psql` shows 2,000 items; `SELECT complete_sale(...)` by hand decrements | 45 min |
| 2 | CMake + presets + `main.cpp` + `catalog_controller` serving `/items` from Postgres | `curl localhost:8080/items \| head -c 200` | 1 h (mostly build setup) |
| 3 | `money`, `catalog`, `transactions` + unit tests | `ctest -L unit` green | 1.5 h |
| 4 | `popularity_window` + unit tests | `ctest -L unit` green | 1 h |
| 5 | `transactions_controller` start / scan / status | `curl` a start + 3 scans; `GET /transactions/{id}` shows count 3 | 1.5 h |
| 6 | `inventory`, `snapshots`, complete, low-stock, popular-items + db tests | `ctest -L db` green; load client runs clean for 10 s | 2 h |
| 7 | `run-test.sh`, both graded runs, `ANALYSIS.md`, `README.md` | two reports + `INVARIANT OK` | 1.5 h |

Total ≈ 9–10 focused hours.

---

## 15. Documented ambiguities and trade-offs

1. **Floor-at-zero.** The load client "sells" the top Zipf item ~200,000 times
   against 10,000 stock. Stock never goes negative; `receipt_lines` still record
   every unit sold. `verify.sql` checks `decremented == LEAST(sold, initial)`.
2. **Open baskets are volatile.** A server crash loses them. Completed sales
   are durable (committed before the 200).
3. **Snapshot insert is not awaited.** A crash in the ~1 ms after a slide can
   lose one snapshot; the next slide replaces it 500 scans later.
4. **`?threshold=` override.** Rows below the override but at/above the
   configured threshold have no `low_stock_since`; `triggeredAt` falls back to
   `generatedAt`.
5. **`limit` > `popular_top_n`** returns `popular_top_n` items (that is all the
   snapshot stores).
6. **Duplicate completion race.** After a successful completion the basket is
   removed; a second `complete` for the same id finds the `sales` row → 409.
   Between the two steps (µs) it would see 404. The client never does this.

## 16. Out of scope this week

`CANCELLED` status (in the enum, no endpoint), authentication, TLS, graceful
drain of open baskets on shutdown, metrics endpoints, Docker image for the
server itself.
