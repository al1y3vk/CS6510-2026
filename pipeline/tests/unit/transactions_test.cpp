#include <catch2/catch_test_macros.hpp>
#include <set>

#include "transactions/transactions.hpp"

namespace {
Catalog tinyCatalog() {
    return Catalog{{{0, "SKU-000001", "Item 1", 85}, {1, "SKU-000002", "Item 2", 120}, {2, "SKU-000003", "Item 3", 155}}};
}
}  // namespace

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

TEST_CASE("scan adds an item and returns the running count/total") {
    Transactions tx;
    auto catalog = tinyCatalog();
    auto basket = tx.start("s");

    auto r1 = tx.scan(*basket, catalog.at(0));
    REQUIRE(r1.has_value());
    REQUIRE(r1->itemCount == 1);
    REQUIRE(r1->totalPriceCents == 85);

    auto r2 = tx.scan(*basket, catalog.at(1));
    REQUIRE(r2->itemCount == 2);
    REQUIRE(r2->totalPriceCents == 205);
}

TEST_CASE("scan on a Completing basket returns nullopt") {
    Transactions tx;
    auto catalog = tinyCatalog();
    auto basket = tx.start("s");
    tx.scan(*basket, catalog.at(0));
    tx.prepareComplete(*basket, catalog);   // flips it to Completing

    REQUIRE_FALSE(tx.scan(*basket, catalog.at(1)).has_value());
}

TEST_CASE("prepareComplete on an empty basket reports EmptyBasket and leaves it Open") {
    Transactions tx;
    auto catalog = tinyCatalog();
    auto basket = tx.start("s");

    auto sale = tx.prepareComplete(*basket, catalog);
    REQUIRE(sale.status == CompleteStatus::EmptyBasket);
    REQUIRE(basket->state == BasketState::Open);
}

TEST_CASE("prepareComplete on a Completing basket reports NotOpen") {
    Transactions tx;
    auto catalog = tinyCatalog();
    auto basket = tx.start("s");
    tx.scan(*basket, catalog.at(0));
    tx.prepareComplete(*basket, catalog);   // first call: Open -> Completing

    auto second = tx.prepareComplete(*basket, catalog);
    REQUIRE(second.status == CompleteStatus::NotOpen);
}

TEST_CASE("prepareComplete groups duplicate scans and sorts lines by sku") {
    Transactions tx;
    auto catalog = tinyCatalog();
    auto basket = tx.start("s");
    tx.scan(*basket, catalog.at(2));   // SKU-000003
    tx.scan(*basket, catalog.at(0));   // SKU-000001
    tx.scan(*basket, catalog.at(2));   // SKU-000003 again

    auto sale = tx.prepareComplete(*basket, catalog);
    REQUIRE(sale.status == CompleteStatus::Ok);
    REQUIRE(basket->state == BasketState::Completing);
    REQUIRE(sale.itemCount == 3);
    REQUIRE(sale.totalPriceCents == 85 + 155 + 155);

    // receipt keeps first-scan order, one entry per distinct item
    REQUIRE(sale.receipt.size() == 2);
    REQUIRE(sale.receipt[0].index == 2);
    REQUIRE(sale.receipt[0].quantity == 2);
    REQUIRE(sale.receipt[1].index == 0);
    REQUIRE(sale.receipt[1].quantity == 1);

    // lines are sorted by sku for complete_sale()
    REQUIRE(sale.lines.size() == 2);
    REQUIRE(sale.lines[0].sku == "SKU-000001");
    REQUIRE(sale.lines[1].sku == "SKU-000003");
    REQUIRE(sale.lines[1].quantity == 2);
}

TEST_CASE("reopen puts a Completing basket back to Open") {
    Transactions tx;
    auto catalog = tinyCatalog();
    auto basket = tx.start("s");
    tx.scan(*basket, catalog.at(0));
    tx.prepareComplete(*basket, catalog);
    REQUIRE(basket->state == BasketState::Completing);

    tx.reopen(*basket);
    REQUIRE(basket->state == BasketState::Open);
}
