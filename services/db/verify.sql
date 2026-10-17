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