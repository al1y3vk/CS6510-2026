### Other queries (used by `inventory.cpp` / `snapshots.cpp`)

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

-- Seed. Same SKU format, names, and price formula as the mock:
--   price = 0.5 + (i % 47) * 0.35  →  cents = 50 + (i % 47) * 35
INSERT INTO items (sku, id, name, price_cents)
SELECT format('SKU-%s', lpad(i::text, 6, '0')), i, 'Item ' || i, 50 + (i % 47) * 35
  FROM generate_series(1, :catalog_size) AS i;

INSERT INTO stock (sku, quantity, initial_quantity)
SELECT sku, :stock_per_item, :stock_per_item FROM items;
```