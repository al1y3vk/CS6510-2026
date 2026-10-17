#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_string.hpp>
#include <json/json.h>
#include <optional>

#include "analytics/filters.hpp"
#include "catalog.hpp"

namespace {
// The week-2 PopularityWindow, rebuilt from the window + rank filters, so the tests below
// check that splitting it into pipeline stages kept the behavior.
class PopularityWindow {
public:
    PopularityWindow(size_t windowSize, size_t slideInterval, size_t topN, size_t catalogSize)
        : window_(windowSize, slideInterval, catalogSize), topN_(topN) {}
    std::optional<Snapshot> record(uint32_t index) {
        if (auto counts = window_.record(index)) return rankTop(*counts, topN_);
        return std::nullopt;
    }
private:
    WindowFilter window_;
    size_t topN_;
};

// Records `n` scans of item `index`; returns the last snapshot produced, if any.
std::optional<Snapshot> scan(PopularityWindow& w, uint32_t index, int n) {
    std::optional<Snapshot> last;
    for (int i = 0; i < n; ++i)
        if (auto s = w.record(index)) last = std::move(s);
    return last;
}

uint32_t countOf(const Snapshot& s, uint32_t index) {
    for (const auto& e : s.top)
        if (e.index == index) return e.count;
    return 0;
}
}  // namespace

TEST_CASE("no snapshot before the first slide; the 500th scan produces 1..500") {
    PopularityWindow w(1000, 500, 10, 2000);
    REQUIRE_FALSE(scan(w, 0, 499).has_value());

    auto s = w.record(0);
    REQUIRE(s.has_value());
    REQUIRE(s->windowStart == 1);
    REQUIRE(s->windowEnd == 500);
    REQUIRE(countOf(*s, 0) == 500);
}

TEST_CASE("window bounds follow the slide: 1..1000 then 501..1500") {
    PopularityWindow w(1000, 500, 10, 2000);
    scan(w, 0, 999);
    auto s1000 = w.record(0);
    REQUIRE(s1000->windowStart == 1);
    REQUIRE(s1000->windowEnd == 1000);

    scan(w, 1, 499);
    auto s1500 = w.record(1);
    REQUIRE(s1500->windowStart == 501);
    REQUIRE(s1500->windowEnd == 1500);
}

TEST_CASE("scans that fall out of the window are evicted from the counts") {
    PopularityWindow w(1000, 500, 10, 2000);
    scan(w, 7, 500);   // item 7 only in scans 1..500
    scan(w, 1, 999);
    auto s1500 = w.record(1);  // window is 501..1500
    REQUIRE(countOf(*s1500, 7) == 0);
    REQUIRE(countOf(*s1500, 1) == 1000);
}

TEST_CASE("top is ordered by count desc then index asc and capped at topN") {
    PopularityWindow w(1000, 500, 3, 2000);
    // 200 x item 5, 200 x item 2, 50 x item 9, 50 x item 0
    scan(w, 5, 200);
    scan(w, 2, 200);
    scan(w, 9, 50);
    auto s = scan(w, 0, 50);
    REQUIRE(s.has_value());
    REQUIRE(s->top.size() == 3);
    REQUIRE(s->top[0].index == 2);
    REQUIRE(s->top[0].count == 200);
    REQUIRE(s->top[1].index == 5);
    REQUIRE(s->top[2].index == 0);   // 50-count tie: index 0 before 9
}

TEST_CASE("fewer distinct items than topN gives a shorter list") {
    PopularityWindow w(1000, 500, 10, 2000);
    scan(w, 3, 250);
    auto s = scan(w, 4, 250);
    REQUIRE(s->top.size() == 2);
}

TEST_CASE("small window end to end: size 4, slide 2, catalog 3") {
    PopularityWindow w(4, 2, 10, 3);
    // scans: 0 0 | 1 2 | 1 1 | 2 2
    w.record(0);
    auto s2 = w.record(0);              // window 1..2 = {0,0}
    REQUIRE(s2->windowStart == 1);
    REQUIRE(s2->windowEnd == 2);
    REQUIRE(countOf(*s2, 0) == 2);

    w.record(1);
    auto s4 = w.record(2);              // window 1..4 = {0,0,1,2}
    REQUIRE(s4->windowStart == 1);
    REQUIRE(countOf(*s4, 0) == 2);
    REQUIRE(countOf(*s4, 1) == 1);
    REQUIRE(countOf(*s4, 2) == 1);

    w.record(1);
    auto s6 = w.record(1);              // window 3..6 = {1,2,1,1}
    REQUIRE(s6->windowStart == 3);
    REQUIRE(s6->windowEnd == 6);
    REQUIRE(countOf(*s6, 0) == 0);
    REQUIRE(countOf(*s6, 1) == 3);
    REQUIRE(countOf(*s6, 2) == 1);
    REQUIRE(s6->top[0].index == 1);

    w.record(2);
    auto s8 = w.record(2);              // window 5..8 = {1,1,2,2}
    REQUIRE(s8->windowStart == 5);
    REQUIRE(countOf(*s8, 1) == 2);
    REQUIRE(countOf(*s8, 2) == 2);
    REQUIRE(s8->top[0].index == 1);     // tie -> lower index first
    REQUIRE(s8->top[1].index == 2);
}

TEST_CASE("format turns ranked indexes into the response-shaped items JSON") {
    Catalog catalog{{{0, "SKU-000001", "Item 1", 85}, {1, "SKU-000002", "Item 2", 120}, {2, "SKU-000003", "Item 3", 155}}};
    Snapshot snap{.windowStart = 501, .windowEnd = 1500,
                  .computedAt = std::chrono::system_clock::now(),
                  .top = {{2, 40}, {0, 12}}};

    SnapshotRow row = formatSnapshot(snap, catalog);
    REQUIRE(row.windowStart == 501);
    REQUIRE(row.windowEnd == 1500);
    REQUIRE_THAT(row.computedAt, Catch::Matchers::Matches(R"(\d{4}-\d{2}-\d{2}T\d{2}:\d{2}:\d{2}\.\d{3}Z)"));

    const Json::Value& items = row.items;
    REQUIRE(items.size() == 2);
    REQUIRE(items[0]["sku"].asString() == "SKU-000003");
    REQUIRE(items[0]["name"].asString() == "Item 3");
    REQUIRE(items[0]["scanCount"].asInt() == 40);
    REQUIRE(items[0]["rank"].asInt() == 1);
    REQUIRE(items[1]["sku"].asString() == "SKU-000001");
    REQUIRE(items[1]["rank"].asInt() == 2);
}
