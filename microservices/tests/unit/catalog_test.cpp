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

TEST_CASE("catalogFromJson reads a GET /items body in order, price to cents") {
    Json::Value body;
    for (const auto& [sku, price] : {std::pair{"SKU-000001", 0.85}, {"SKU-000002", 1.2}, {"SKU-000003", 16.6}}) {
        Json::Value item;
        item["sku"] = sku;
        item["name"] = std::string("Item ") + sku[9];
        item["price"] = price;
        body["items"].append(item);
    }
    auto c = catalogFromJson(body);

    REQUIRE(c.size() == 3);
    REQUIRE(c.at(0).sku == "SKU-000001");
    REQUIRE(c.at(0).price_cents == 85);
    REQUIRE(c.at(1).price_cents == 120);
    REQUIRE(c.at(2).price_cents == 1660);   // 16.6 * 100 = 1659.999…, must round, not truncate
    REQUIRE(c.find("SKU-000002")->index == 1);
    REQUIRE(c.at(2).name == "Item 3");
}

TEST_CASE("catalogFromJson rejects an empty or malformed body") {
    REQUIRE_THROWS(catalogFromJson(Json::Value{}));
    Json::Value bad;
    bad["items"][0]["sku"] = "SKU-000001";   // no name / price
    REQUIRE_THROWS(catalogFromJson(bad));
}
