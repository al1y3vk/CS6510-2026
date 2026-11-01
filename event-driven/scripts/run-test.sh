#!/usr/bin/env bash
# usage: scripts/run-test.sh [load-client flags...]
#   scripts/run-test.sh                                 # default 10 stations, 60 s
#   scripts/run-test.sh --stations=100 --duration=120   # stress
set -euo pipefail
ROOT=$(cd "$(dirname "$0")/.." && pwd)

docker stop checkout-gateway ms-gateway >/dev/null 2>&1 || true   # weeks 4-5 gateways also want :8080
(cd "$ROOT" && docker compose up -d broker gateway)
"$ROOT/db/reset.sh"
"$ROOT/broker/reset.sh"
PIDS=()
for svc in catalog checkout inventory analytics; do   # any order: the others wait for the catalog event
    "$ROOT/build/release/$svc-service" "$ROOT/config/$svc.json" &
    PIDS+=($!)
done
trap 'kill "${PIDS[@]}" 2>/dev/null || true' EXIT

# Ready = each route answers through the gateway with something other than 502 (down) / 503 (starting).
for path in /items /transactions/none /inventory/low-stock /analytics/popular-items; do
    until code=$(curl -s -o /dev/null -w '%{http_code}' "http://localhost:8080$path") &&
          [[ $code != 000 && $code != 502 && $code != 503 ]]; do sleep 0.2; done
done

(cd "$ROOT/../load-client" && bash ./run.sh --reportDir="$ROOT/reports" "$@")   # given script, not executable in git

"$ROOT/scripts/verify.sh"
