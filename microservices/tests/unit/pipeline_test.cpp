#include <catch2/catch_test_macros.hpp>
#include <json/json.h>
#include <optional>
#include <thread>
#include <vector>

#include "analytics/pipeline.hpp"
#include "catalog.hpp"

namespace {
Catalog tinyCatalog() {
    return Catalog{{{0, "SKU-000001", "Item 1", 85}, {1, "SKU-000002", "Item 2", 120}, {2, "SKU-000003", "Item 3", 155}}};
}

// Flattens the persist filter's batches back into one row list.
AnalyticsPipeline::Sink collectInto(std::vector<SnapshotRow>& rows) {
    return [&rows](const std::vector<SnapshotRow>& batch) { rows.insert(rows.end(), batch.begin(), batch.end()); };
}
}  // namespace

TEST_CASE("scans flow window -> rank -> format -> sink, one row per slide") {
    auto catalog = tinyCatalog();
    std::vector<SnapshotRow> rows;   // only the persist thread writes; read after it is joined
    {
        AnalyticsPipeline p(4, 2, 10, catalog, collectInto(rows));
        for (uint32_t i : {0, 0, 1, 2, 1}) p.submit(i);
    }  // destructor closes the input and drains every stage

    REQUIRE(rows.size() == 2);
    REQUIRE(rows[0].windowStart == 1);
    REQUIRE(rows[0].windowEnd == 2);
    REQUIRE(rows[1].windowEnd == 4);

    const auto& items = rows[1].items;   // window 1..4 = {0,0,1,2}
    REQUIRE(items.size() == 3);
    REQUIRE(items[0]["sku"].asString() == "SKU-000001");
    REQUIRE(items[0]["scanCount"].asInt() == 2);
    REQUIRE(items[1]["sku"].asString() == "SKU-000002");
    REQUIRE(items[2]["sku"].asString() == "SKU-000003");
}

TEST_CASE("concurrent submitters: every scan is counted exactly once") {
    auto catalog = tinyCatalog();
    std::vector<SnapshotRow> rows;
    {
        AnalyticsPipeline p(1000, 500, 10, catalog, collectInto(rows));
        std::vector<std::jthread> stations;
        for (uint32_t t = 0; t < 4; ++t)
            stations.emplace_back([&p, t] { for (int i = 0; i < 1000; ++i) p.submit(t % 3); });
    }  // stations join first (declared later), then the pipeline drains

    REQUIRE(rows.size() == 8);   // 4000 scans / slide 500
    for (size_t i = 0; i < rows.size(); ++i) REQUIRE(rows[i].windowEnd == (i + 1) * 500);

    const auto& last = rows.back().items;
    REQUIRE(rows.back().windowStart == 3001);
    int total = 0;
    for (const auto& item : last) total += item["scanCount"].asInt();
    REQUIRE(total == 1000);
}
