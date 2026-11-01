#include <catch2/catch_test_macros.hpp>
#include <drogon/utils/coroutine.h>

#include "analytics/filters.hpp"
#include "db/snapshots.hpp"
#include "db_fixture.hpp"

namespace {
SnapshotRow row(uint64_t start, uint64_t end, const std::string& sku) {
    Json::Value item;
    item["sku"] = sku;
    item["scanCount"] = 40;
    Json::Value items(Json::arrayValue);
    items.append(item);
    return SnapshotRow{.windowStart = start, .windowEnd = end,
                       .computedAt = "2026-10-02T12:00:00.000Z", .items = items};
}
}  // namespace

TEST_CASE("latest is empty when nothing was ever inserted", "[db]") {
    TinyDb db("analytics");
    Snapshots snapshots(db.client);
    REQUIRE_FALSE(drogon::sync_wait(snapshots.latest()).has_value());
}

TEST_CASE("insert stores the row and latest reads it back", "[db]") {
    TinyDb db("analytics");
    Snapshots snapshots(db.client);

    snapshots.insert({row(501, 1500, "SKU-000003")});

    auto stored = drogon::sync_wait(snapshots.latest());
    REQUIRE(stored.has_value());
    REQUIRE(stored->windowStart == 501);
    REQUIRE(stored->windowEnd == 1500);
    REQUIRE(stored->computedAt == "2026-10-02T12:00:00.000Z");
    REQUIRE(stored->items.size() == 1);
    REQUIRE(stored->items[0]["sku"].asString() == "SKU-000003");
    REQUIRE(stored->items[0]["scanCount"].asInt() == 40);
}

TEST_CASE("a batch is inserted in order, so latest is its last row", "[db]") {
    TinyDb db("analytics");
    Snapshots snapshots(db.client);

    snapshots.insert({row(1, 500, "SKU-000001"), row(1, 1000, "SKU-000002"), row(501, 1500, "SKU-000003")});

    REQUIRE(db.scalar<int>("SELECT count(*) FROM popular_snapshots") == 3);
    auto newest = drogon::sync_wait(snapshots.latest());
    REQUIRE(newest->windowEnd == 1500);
    REQUIRE(newest->items[0]["sku"].asString() == "SKU-000003");
}
