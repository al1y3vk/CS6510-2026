# Layered self-checkout server

C++20 / Drogon 1.8 / Postgres 18. Same API contract and behavior as `monolith/`, restructured
into four layers: `src/api` (HTTP), `src/transactions` (basket state + sale orchestration),
`src/analytics` (popularity window + snapshot assembly), `src/db` (raw SQL). Prereqs:
`libdrogon-dev`, `catch2`, `cmake`, `ninja-build`, `docker`, `psql`.

```bash
docker compose up -d --wait                                   # Postgres on :5432
db/reset.sh                                                   # drop + create + seed 2,000 items
cmake --preset release && cmake --build --preset release      # -> build/release/checkout-server
./build/release/checkout-server config.json                   # http://localhost:8080
scripts/run-test.sh [--stations=100 --duration=120]           # reset -> server -> load client -> invariant check
```

Tests: `ctest --preset release -L unit` (no DB) · `ctest --preset release -L db` (needs compose up).
