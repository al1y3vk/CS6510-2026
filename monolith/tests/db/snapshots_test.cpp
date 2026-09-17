#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_string.hpp>
#include <chrono>
#include <drogon/utils/coroutine.h>
#include <thread>

#include "catalog.hpp"
#include "db_fixture.hpp"
#include "popularity_window.hpp"
#include "snapshots.hpp"

using Catch::Matchers::Matches;

namespace {
Catalog tinyCatalog() {
    return Catalog{{{0, "SKU-000001", "Item 1", 85}, {1, "SKU-000002", "Item 2", 120}, {2, "SKU-000003", "Item 3", 155}}};
}

// insertAsync is fire-and-forget, so poll until the row lands (or give up after ~2 s).
std::optional<StoredSnapshot> waitForLatest(Snapshots& snapshots) {
    for (int i = 0; i < 40; ++i) {
        if (auto s = drogon::sync_wait(snapshots.latest())) return s;
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
    }
    return std::nullopt;
}
}  // namespace

TEST_CASE("latest is empty when nothing was ever inserted", "[db]") {
    TinyDb db;
    Snapshots snapshots(db.client);
    REQUIRE_FALSE(drogon::sync_wait(snapshots.latest()).has_value());
}

TEST_CASE("insertAsync stores the snapshot in response shape and latest reads it back", "[db]") {
    TinyDb db;
    Snapshots snapshots(db.client);
    auto catalog = tinyCatalog();

    Snapshot snap{.windowStart = 501, .windowEnd = 1500,
                  .computedAt = std::chrono::system_clock::now(),
                  .top = {{2, 40}, {0, 12}}};
    snapshots.insertAsync(snap, catalog);

    auto stored = waitForLatest(snapshots);
    REQUIRE(stored.has_value());
    REQUIRE(stored->windowStart == 501);
    REQUIRE(stored->windowEnd == 1500);
    REQUIRE_THAT(stored->computedAt, Matches(R"(\d{4}-\d{2}-\d{2}T\d{2}:\d{2}:\d{2}\.\d{3}Z)"));
    REQUIRE(stored->items.isArray());
    REQUIRE(stored->items.size() == 2);
    REQUIRE(stored->items[0]["sku"].asString() == "SKU-000003");
    REQUIRE(stored->items[0]["name"].asString() == "Item 3");
    REQUIRE(stored->items[0]["scanCount"].asInt() == 40);
    REQUIRE(stored->items[0]["rank"].asInt() == 1);
    REQUIRE(stored->items[1]["sku"].asString() == "SKU-000001");
    REQUIRE(stored->items[1]["rank"].asInt() == 2);
}

TEST_CASE("latest returns the newest snapshot", "[db]") {
    TinyDb db;
    Snapshots snapshots(db.client);
    auto catalog = tinyCatalog();

    snapshots.insertAsync(Snapshot{.windowStart = 1, .windowEnd = 500, .computedAt = std::chrono::system_clock::now(), .top = {{0, 5}}}, catalog);
    REQUIRE(waitForLatest(snapshots).has_value());
    snapshots.insertAsync(Snapshot{.windowStart = 1, .windowEnd = 1000, .computedAt = std::chrono::system_clock::now(), .top = {{1, 9}}}, catalog);

    std::optional<StoredSnapshot> newest;
    for (int i = 0; i < 40 && !(newest && newest->windowEnd == 1000); ++i) {
        newest = drogon::sync_wait(snapshots.latest());
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
    }
    REQUIRE(newest.has_value());
    REQUIRE(newest->windowEnd == 1000);
    REQUIRE(newest->items[0]["sku"].asString() == "SKU-000002");
}
