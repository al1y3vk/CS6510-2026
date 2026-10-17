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