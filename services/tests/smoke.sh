#!/usr/bin/env bash
# HTTP smoke test: every endpoint and error code once, against a running server with a fresh DB.
#   db/reset.sh && ./build/debug/checkout-server config.json &
#   tests/smoke.sh
set -uo pipefail
BASE=${BASE:-http://localhost:8080}
BODY=$(mktemp)
trap 'rm -f "$BODY"' EXIT
fail=0

# check <name> <expected status> <method> <path> [json body]
check() {
    local name=$1 want=$2 method=$3 path=$4 data=${5:-}
    local got
    got=$(curl -s -o "$BODY" -w '%{http_code}' -X "$method" -H 'Content-Type: application/json' ${data:+-d "$data"} "$BASE$path")
    if [ "$got" = "$want" ]; then echo "ok   $name ($got)"; else echo "FAIL $name: want $want, got $got: $(cat "$BODY")"; fail=1; fi
}
# field <python expression over d> <expected>
field() {
    local got
    got=$(python3 -c "import json,sys; d=json.load(open(sys.argv[1])); print($1)" "$BODY")
    if [ "$got" = "$2" ]; then echo "     $1 = $got"; else echo "FAIL $1: want $2, got $got"; fail=1; fi
}

check "GET /items" 200 GET /items
field 'len(d["items"])' 2000
field 'd["items"][0]["sku"]' SKU-000001
field 'd["items"][0]["price"]' 0.85

check "start: bad body" 400 POST /transactions '{}'
field 'd["error"]' INVALID_REQUEST
check "start" 201 POST /transactions '{"stationId":"station-1"}'
field 'd["status"]' OPEN
field 'd["runningTotal"]' 0.0
TX=$(python3 -c 'import json,sys; print(json.load(open(sys.argv[1]))["transactionId"])' "$BODY")

check "scan: missing sku" 400 POST "/transactions/$TX/items" '{}'
check "scan: unknown sku" 404 POST "/transactions/$TX/items" '{"sku":"SKU-999999"}'
field 'd["error"]' UNKNOWN_SKU
check "scan: unknown tx" 404 POST "/transactions/tx-nope/items" '{"sku":"SKU-000001"}'
field 'd["error"]' NOT_FOUND
check "scan 1" 200 POST "/transactions/$TX/items" '{"sku":"SKU-000001"}'
field 'd["itemCount"]' 1
field 'd["unitPrice"]' 0.85
check "scan 2 (same sku)" 200 POST "/transactions/$TX/items" '{"sku":"SKU-000001"}'
check "scan 3" 200 POST "/transactions/$TX/items" '{"sku":"SKU-000003"}'
field 'd["itemCount"]' 3
field 'd["runningTotal"]' 3.25

check "status: open" 200 GET "/transactions/$TX"
field 'd["status"]' OPEN
field 'd["itemCount"]' 3

check "start empty" 201 POST /transactions '{"stationId":"station-2"}'
EMPTY=$(python3 -c 'import json,sys; print(json.load(open(sys.argv[1]))["transactionId"])' "$BODY")
check "complete: empty basket" 409 POST "/transactions/$EMPTY/complete"
field 'd["error"]' EMPTY_BASKET
check "complete: unknown tx" 404 POST "/transactions/tx-nope/complete"

check "complete" 200 POST "/transactions/$TX/complete"
field 'd["itemCount"]' 3
field 'd["totalAmount"]' 3.25
field 'len(d["lines"])' 2
field 'd["lines"][0]["sku"]' SKU-000001
field 'd["lines"][0]["quantity"]' 2
check "complete again" 409 POST "/transactions/$TX/complete"
field 'd["error"]' TRANSACTION_NOT_OPEN
check "scan after complete" 409 POST "/transactions/$TX/items" '{"sku":"SKU-000001"}'
check "status: completed" 200 GET "/transactions/$TX"
field 'd["status"]' COMPLETED
field 'd["runningTotal"]' 3.25
check "status: unknown" 404 GET "/transactions/tx-nope"

check "low-stock: default threshold" 200 GET /inventory/low-stock
field 'd["threshold"]' 50
field 'len(d["alerts"])' 0
check "low-stock: threshold=10000" 200 GET "/inventory/low-stock?threshold=10000"
field 'len(d["alerts"])' 2
field 'd["alerts"][0]["sku"]' SKU-000001
field 'd["alerts"][0]["currentStock"]' 9998
check "low-stock: negative" 400 GET "/inventory/low-stock?threshold=-1"
check "low-stock: junk" 400 GET "/inventory/low-stock?threshold=abc"

check "popular: no snapshot yet" 200 GET /analytics/popular-items
field 'd["windowSize"]' 1000
field 'd["windowEnd"]' 0
field 'd["items"]' '[]'
check "popular: junk limit" 400 GET "/analytics/popular-items?limit=abc"

check "unknown route" 404 GET /nope

[ $fail -eq 0 ] && echo "SMOKE OK" || { echo "SMOKE FAILED"; exit 1; }
