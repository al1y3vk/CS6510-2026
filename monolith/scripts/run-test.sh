#!/usr/bin/env bash
# usage: scripts/run-test.sh [load-client flags...]
#   scripts/run-test.sh                                 # default 10 stations, 60 s
#   scripts/run-test.sh --stations=100 --duration=120   # stress
set -euo pipefail
ROOT=$(cd "$(dirname "$0")/.." && pwd)
: "${PGURL:=postgresql://checkout:checkout@localhost:5432/checkout}"

"$ROOT/db/reset.sh"
"$ROOT/build/release/checkout-server" "$ROOT/config.json" &
SERVER=$!
trap 'kill "$SERVER" 2>/dev/null || true' EXIT
until curl -sf http://localhost:8080/items >/dev/null; do sleep 0.2; done

(cd "$ROOT/../load-client" && bash ./run.sh --reportDir="$ROOT/reports" "$@")   # given script, not executable in git

MISMATCHES=$(psql "$PGURL" -tA -f "$ROOT/db/verify.sql" | wc -l)
if [ "$MISMATCHES" -eq 0 ]; then
    echo "INVARIANT OK (0 mismatches)"
else
    echo "INVARIANT VIOLATED ($MISMATCHES SKUs):"
    psql "$PGURL" -f "$ROOT/db/verify.sql"
    exit 1
fi
