#!/usr/bin/env bash
# Cross-service invariant, run after a load test: every sale checkout recorded reached inventory
# exactly once (outbox -> sales.completed -> inventory), and each SKU's stock movement matches the
# units on checkout's receipts. Reads both databases from outside; the services never do this.
set -euo pipefail
: "${PGSERVER:=postgresql://checkout:checkout@localhost:5432}"
count() { psql "$PGSERVER/$1" -tAc "SELECT count(*) FROM $2"; }

# Delivery is asynchronous: wait (up to 30 s) until the outbox is empty and inventory has applied
# as many sales as checkout recorded.
for _ in $(seq 150); do
    [ "$(count checkout sale_outbox)" -eq 0 ] && [ "$(count checkout sales)" -eq "$(count inventory applied_sales)" ] && break
    sleep 0.2
done

SALES=$(count checkout sales)
PENDING=$(count checkout sale_outbox)
APPLIED=$(count inventory applied_sales)
echo "sales recorded by checkout: $SALES · still in outbox: $PENDING · applied by inventory: $APPLIED"

# Units sold per SKU (checkout db) copied into a temp table in the inventory db, then compared.
# Floor-at-zero means decremented = LEAST(sold, initial).
MISMATCHES=$(psql "$PGSERVER/checkout" -tAc \
        "COPY (SELECT sku, SUM(quantity) FROM receipt_lines GROUP BY sku) TO STDOUT" |
    psql "$PGSERVER/inventory" -qtA \
        -c "CREATE TEMP TABLE sold (sku TEXT PRIMARY KEY, units BIGINT)" \
        -c "COPY sold FROM STDIN" \
        -c "SELECT s.sku FROM stock s LEFT JOIN sold USING (sku)
             WHERE s.initial_quantity - s.quantity <> LEAST(COALESCE(sold.units, 0), s.initial_quantity)" |
    grep -c . || true)

if [ "$PENDING" -eq 0 ] && [ "$SALES" -eq "$APPLIED" ] && [ "$MISMATCHES" -eq 0 ]; then
    echo "INVARIANT OK (0 mismatches)"
else
    echo "INVARIANT VIOLATED ($MISMATCHES SKUs, $PENDING pending, $SALES sales vs $APPLIED applied)"
    exit 1
fi
