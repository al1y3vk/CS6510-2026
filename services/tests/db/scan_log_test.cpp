#include <catch2/catch_test_macros.hpp>

#include "db/scan_log.hpp"
#include "db_fixture.hpp"

TEST_CASE("after returns appended scans in order, then only newer ones", "[db]") {
    TinyDb db;
    ScanLog log(db.client);

    REQUIRE(log.after(0, 10).empty());
    log.append({4, 0, 4});
    log.append({2});

    auto all = log.after(0, 10);
    REQUIRE(all.size() == 4);
    REQUIRE(all[0].itemIndex == 4);
    REQUIRE(all[1].itemIndex == 0);
    REQUIRE(all[2].itemIndex == 4);
    REQUIRE(all[3].itemIndex == 2);
    REQUIRE(all[0].id < all[1].id);
    REQUIRE(all[2].id < all[3].id);

    auto newer = log.after(all[1].id, 10);
    REQUIRE(newer.size() == 2);
    REQUIRE(newer[0].id == all[2].id);
}

TEST_CASE("after caps the batch at limit", "[db]") {
    TinyDb db;
    ScanLog log(db.client);
    log.append({1, 2, 3});

    auto first = log.after(0, 2);
    REQUIRE(first.size() == 2);
    REQUIRE(log.after(first.back().id, 2).size() == 1);
}
