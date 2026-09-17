#include <catch2/catch_test_macros.hpp>
#include <set>

#include "transactions.hpp"

TEST_CASE("start returns an open, empty basket for the station") {
    Transactions tx;
    auto b = tx.start("station-7");

    REQUIRE(b != nullptr);
    REQUIRE(b->id.rfind("tx-", 0) == 0);
    REQUIRE(b->id.size() == 3 + 32);
    REQUIRE(b->stationId == "station-7");
    REQUIRE(b->state == BasketState::Open);
    REQUIRE(b->itemIndices.empty());
    REQUIRE(b->totalPriceCents == 0);
    REQUIRE(tx.openCount() == 1);
}

TEST_CASE("find returns the started basket and nullptr for unknown ids") {
    Transactions tx;
    auto b = tx.start("s");

    REQUIRE(tx.find(b->id) == b);
    REQUIRE(tx.find("tx-doesnotexist") == nullptr);
}

TEST_CASE("remove makes a basket unfindable") {
    Transactions tx;
    auto b = tx.start("s");
    tx.remove(b->id);

    REQUIRE(tx.find(b->id) == nullptr);
    REQUIRE(tx.openCount() == 0);
}

TEST_CASE("ids never repeat") {
    Transactions tx;
    std::set<std::string> ids;
    for (int i = 0; i < 10'000; ++i) ids.insert(tx.start("s")->id);
    REQUIRE(ids.size() == 10'000);
}
