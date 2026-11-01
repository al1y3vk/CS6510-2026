#include <catch2/catch_test_macros.hpp>

#include "catalog.hpp"
#include "events.hpp"

TEST_CASE("a sale event survives a JSON round trip") {
    SaleEvent sent{"tx-1", {{"SKU-000001", 2}, {"SKU-000003", 1}}};
    auto parsed = parseSaleEvent(saleEventJson(sent));

    REQUIRE(parsed.has_value());
    REQUIRE(parsed->transactionId == "tx-1");
    REQUIRE(parsed->lines.size() == 2);
    REQUIRE(parsed->lines[1].sku == "SKU-000003");
    REQUIRE(parsed->lines[0].quantity == 2);
}

TEST_CASE("parseSaleEvent rejects malformed values") {
    REQUIRE(parseSaleEvent(R"({"transactionId":"tx-1","lines":[{"sku":"SKU-000001","quantity":2}]})").has_value());

    REQUIRE_FALSE(parseSaleEvent("").has_value());
    REQUIRE_FALSE(parseSaleEvent("not json").has_value());
    REQUIRE_FALSE(parseSaleEvent("[]").has_value());
    REQUIRE_FALSE(parseSaleEvent(R"({"transactionId":"tx-1","lines":[]})").has_value());
    REQUIRE_FALSE(parseSaleEvent(R"({"transactionId":"tx-1","lines":[5]})").has_value());
    REQUIRE_FALSE(parseSaleEvent(R"({"transactionId":"tx-1","lines":[{"sku":"SKU-000001","quantity":0}]})").has_value());
    REQUIRE_FALSE(parseSaleEvent(R"({"lines":[{"sku":"SKU-000001","quantity":2}]})").has_value());
}

TEST_CASE("a scan event survives a JSON round trip") {
    auto parsed = parseScanEvent(scanEventJson({"tx-1", "SKU-000042"}));

    REQUIRE(parsed.has_value());
    REQUIRE(parsed->transactionId == "tx-1");
    REQUIRE(parsed->sku == "SKU-000042");
}

TEST_CASE("parseScanEvent rejects malformed values") {
    REQUIRE_FALSE(parseScanEvent("").has_value());
    REQUIRE_FALSE(parseScanEvent(R"({"transactionId":"tx-1"})").has_value());
    REQUIRE_FALSE(parseScanEvent(R"({"transactionId":"tx-1","sku":7})").has_value());
}

TEST_CASE("the catalog event carries the whole catalog, in order") {
    Catalog sent{{{0, "SKU-000001", "Milk", 85}, {1, "SKU-000002", "Bread", 250}}};
    auto json = parseJson(toJson(catalogJson(sent)));
    REQUIRE(json.has_value());

    Catalog got = catalogFromJson(*json);
    REQUIRE(got.size() == 2);
    REQUIRE(got.at(1).sku == "SKU-000002");
    REQUIRE(got.at(1).name == "Bread");
    REQUIRE(got.at(1).price_cents == 250);
}
