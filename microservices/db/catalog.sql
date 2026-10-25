-- Catalog service's own database. Idempotent: drop, recreate, seed.
DROP TABLE IF EXISTS items CASCADE;
\ir common.sql

CREATE TABLE items (
    sku         TEXT PRIMARY KEY,
    id          INT  NOT NULL UNIQUE,          -- catalog order = Zipf rank (1-based)
    name        TEXT NOT NULL,
    price_cents INT  NOT NULL CHECK (price_cents > 0)
);

-- Same SKU format, names, and price formula as the mock:
--   price = 0.5 + (i % 47) * 0.35  →  cents = 50 + (i % 47) * 35
INSERT INTO items (sku, id, name, price_cents)
SELECT format('SKU-%s', lpad(i::text, 6, '0')), i, 'Item ' || i, 50 + (i % 47) * 35
  FROM generate_series(1, :catalog_size) AS i;
