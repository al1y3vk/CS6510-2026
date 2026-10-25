#include <catch2/catch_test_macros.hpp>

#include "sale_event.hpp"

TEST_CASE("sale events survive a JSON round trip") {
    std::vector<SaleEvent> sent{
        {"tx-1", {{"SKU-000001", 2}, {"SKU-000003", 1}}},
        {"tx-2", {{"SKU-000002", 5}}},
    };
    auto parsed = parseSaleEvents(saleEventsJson(sent));

    REQUIRE(parsed.has_value());
    REQUIRE(parsed->size() == 2);
    REQUIRE((*parsed)[0].transactionId == "tx-1");
    REQUIRE((*parsed)[0].lines.size() == 2);
    REQUIRE((*parsed)[0].lines[1].sku == "SKU-000003");
    REQUIRE((*parsed)[1].lines[0].quantity == 5);
}

TEST_CASE("parseSaleEvents rejects malformed bodies") {
    Json::Value good = saleEventsJson({{"tx-1", {{"SKU-000001", 2}}}});
    REQUIRE(parseSaleEvents(good).has_value());

    REQUIRE_FALSE(parseSaleEvents(Json::Value{}).has_value());

    Json::Value noLines = good;
    noLines["sales"][0]["lines"] = Json::Value(Json::arrayValue);
    REQUIRE_FALSE(parseSaleEvents(noLines).has_value());

    Json::Value zeroQty = good;
    zeroQty["sales"][0]["lines"][0]["quantity"] = 0;
    REQUIRE_FALSE(parseSaleEvents(zeroQty).has_value());

    Json::Value noId = good;
    noId["sales"][0].removeMember("transactionId");
    REQUIRE_FALSE(parseSaleEvents(noId).has_value());
}

TEST_CASE("an empty batch is valid") {
    auto parsed = parseSaleEvents(saleEventsJson({}));
    REQUIRE(parsed.has_value());
    REQUIRE(parsed->empty());
}
