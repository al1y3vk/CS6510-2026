-- Inventory service's own database: stock levels plus the sales it has already applied.
DROP TABLE IF EXISTS applied_sales, stock CASCADE;
DROP FUNCTION IF EXISTS apply_sales(TEXT[], TEXT[], TEXT[], INT[], INT);
\ir common.sql

CREATE TABLE stock (
    sku              TEXT PRIMARY KEY,
    quantity         INT  NOT NULL CHECK (quantity >= 0),
    initial_quantity INT  NOT NULL,
    low_stock_since  TIMESTAMPTZ                -- set once, first time quantity < threshold
);

-- Idempotency keys: sales.completed is delivered at least once (outbox relay retries, redelivery
-- of uncommitted offsets), so a sale can arrive twice.
CREATE TABLE applied_sales (
    transaction_id TEXT PRIMARY KEY,
    applied_at     TIMESTAMPTZ NOT NULL DEFAULT clock_timestamp()
);

-- Applies a batch of sale-completed events in one transaction. Lines arrive flattened:
-- p_line_tx[i] / p_skus[i] / p_qtys[i] is one receipt line of sale p_line_tx[i].
-- Sales already in applied_sales are skipped. Returns how many sales were new.
CREATE FUNCTION apply_sales(
    p_tx_ids    TEXT[],
    p_line_tx   TEXT[],
    p_skus      TEXT[],
    p_qtys      INT[],
    p_threshold INT
) RETURNS INT LANGUAGE plpgsql AS $$
DECLARE
    v_now TIMESTAMPTZ := clock_timestamp();
    v_new TEXT[];
BEGIN
    WITH ins AS (
        INSERT INTO applied_sales (transaction_id)
        SELECT DISTINCT t FROM unnest(p_tx_ids) AS t
        ON CONFLICT DO NOTHING
        RETURNING transaction_id
    )
    SELECT coalesce(array_agg(transaction_id), '{}') INTO v_new FROM ins;

    -- Decrements commute, so one summed UPDATE per SKU ends at the same quantity as one per sale,
    -- floor at zero included.
    UPDATE stock s
       SET quantity        = GREATEST(0, s.quantity - b.qty),
           low_stock_since = CASE
                                 WHEN s.low_stock_since IS NULL
                                  AND GREATEST(0, s.quantity - b.qty) < p_threshold
                                 THEN v_now
                                 ELSE s.low_stock_since
                             END
      FROM (SELECT l.sku, SUM(l.qty)::INT AS qty
              FROM unnest(p_line_tx, p_skus, p_qtys) AS l(tx, sku, qty)
             WHERE l.tx = ANY(v_new)
             GROUP BY l.sku) AS b
     WHERE s.sku = b.sku;

    RETURN cardinality(v_new);
END
$$;

-- Seeded with the same SKU list the catalog generates.
INSERT INTO stock (sku, quantity, initial_quantity)
SELECT format('SKU-%s', lpad(i::text, 6, '0')), :stock_per_item, :stock_per_item
  FROM generate_series(1, :catalog_size) AS i;
