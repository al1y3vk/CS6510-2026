#!/usr/bin/env bash
# Drop + recreate + seed every service's database. Run before every test run.
#   CATALOG_SIZE=5 STOCK_PER_ITEM=3 ./reset.sh   (tiny DBs for the db tests)
# The four databases share one Postgres server (the checkout-db container) to save setup;
# each service's config.json names only its own database.
set -euo pipefail
cd "$(dirname "$0")"
: "${PGSERVER:=postgresql://checkout:checkout@localhost:5432}"
: "${CATALOG_SIZE:=2000}"
: "${STOCK_PER_ITEM:=10000}"
for db in catalog checkout inventory analytics; do
    if [ -z "$(psql "$PGSERVER/postgres" -tAc "SELECT 1 FROM pg_database WHERE datname = '$db'")" ]; then
        psql "$PGSERVER/postgres" -qc "CREATE DATABASE $db"
    fi
    PGOPTIONS='--client-min-messages=warning' psql "$PGSERVER/$db" -q -v ON_ERROR_STOP=1 \
         -v catalog_size="$CATALOG_SIZE" -v stock_per_item="$STOCK_PER_ITEM" -f "$db.sql"
done
echo "db reset: catalog, checkout, inventory, analytics ($CATALOG_SIZE items × $STOCK_PER_ITEM stock)"
