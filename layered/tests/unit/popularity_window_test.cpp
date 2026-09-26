#include <catch2/catch_test_macros.hpp>
#include <optional>

#include "analytics/popularity_window.hpp"

namespace {
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
