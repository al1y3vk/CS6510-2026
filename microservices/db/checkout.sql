-- Checkout service's own database: completed sales plus the outbox that tells inventory about them.
DROP TABLE IF EXISTS sale_outbox, receipt_lines, sales CASCADE;
DROP FUNCTION IF EXISTS complete_sale(TEXT, TEXT, TIMESTAMPTZ, TEXT[], INT[], INT[], INT, BIGINT);
\ir common.sql

CREATE TABLE sales (
    transaction_id TEXT PRIMARY KEY,
    station_id     TEXT        NOT NULL,
    started_at     TIMESTAMPTZ NOT NULL,
    completed_at   TIMESTAMPTZ NOT NULL,
    item_count     INT         NOT NULL,
    total_cents    BIGINT      NOT NULL
);

-- No FK to items: the catalog lives in another service's database.
CREATE TABLE receipt_lines (
    transaction_id   TEXT NOT NULL REFERENCES sales(transaction_id),
    sku              TEXT NOT NULL,
    quantity         INT  NOT NULL CHECK (quantity > 0),
    unit_price_cents INT  NOT NULL,
    PRIMARY KEY (transaction_id, sku)
);

-- Transactional outbox: one row per completed sale, written in the same transaction as the sale.
-- The relay thread sends pending rows to inventory and deletes them once inventory has applied them.
CREATE TABLE sale_outbox (
    id             BIGSERIAL PRIMARY KEY,
    transaction_id TEXT NOT NULL REFERENCES sales(transaction_id)
);

-- The whole completion in one statement = one local transaction = one round-trip.
-- Caller passes arrays sorted by SKU with no duplicates. Stock is inventory's; it is not touched here.
CREATE FUNCTION complete_sale(
    p_tx_id       TEXT,
    p_station_id  TEXT,
    p_started_at  TIMESTAMPTZ,
    p_skus        TEXT[],
    p_qtys        INT[],
    p_unit_cents  INT[],
    p_item_count  INT,
    p_total_cents BIGINT
) RETURNS TIMESTAMPTZ LANGUAGE plpgsql AS $$
DECLARE
    v_now TIMESTAMPTZ := clock_timestamp();
BEGIN
    INSERT INTO sales (transaction_id, station_id, started_at, completed_at, item_count, total_cents)
    VALUES (p_tx_id, p_station_id, p_started_at, v_now, p_item_count, p_total_cents);

    INSERT INTO receipt_lines (transaction_id, sku, quantity, unit_price_cents)
    SELECT p_tx_id, b.sku, b.qty, b.cents
      FROM unnest(p_skus, p_qtys, p_unit_cents) AS b(sku, qty, cents);

    INSERT INTO sale_outbox (transaction_id) VALUES (p_tx_id);
    RETURN v_now;
END
$$;
