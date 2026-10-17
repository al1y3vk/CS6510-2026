#include <catch2/catch_test_macros.hpp>

#include "catalog.hpp"

namespace {
Catalog threeItems() {
    return Catalog{{{0, "SKU-000001", "Item 1", 85}, {1, "SKU-000002", "Item 2", 120}, {2, "SKU-000003", "Item 3", 155}}};
}
}  // namespace

TEST_CASE("at returns the item at its index") {
    auto c = threeItems();
    REQUIRE(c.size() == 3);
    REQUIRE(c.at(1).sku == "SKU-000002");
    REQUIRE(c.at(2).price_cents == 155);
    REQUIRE(c.all().size() == 3);
}

TEST_CASE("find looks up by sku and returns nullptr when unknown") {
    auto c = threeItems();
    const Item* found = c.find("SKU-000003");
    REQUIRE(found != nullptr);
    REQUIRE(found->index == 2);
    REQUIRE(c.find("SKU-999999") == nullptr);
}
