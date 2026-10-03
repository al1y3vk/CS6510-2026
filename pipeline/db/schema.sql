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
    items        JSONB       NOT NULL
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
