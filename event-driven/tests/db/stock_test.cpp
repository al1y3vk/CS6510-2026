#include <catch2/catch_test_macros.hpp>
#include <drogon/utils/coroutine.h>

#include "db/stock.hpp"
#include "db_fixture.hpp"

namespace {
int qty(TinyDb& db, const std::string& sku) {
    return db.scalar<int>("SELECT quantity FROM stock WHERE sku = '" + sku + "'");
}
}  // namespace

TEST_CASE("applySales decrements stock for every line of every sale", "[db]") {
    TinyDb db("inventory");
    Stock stock(db.client, 2);

    int applied = drogon::sync_wait(stock.applySales({
        {"tx-a", {{"SKU-000001", 2}, {"SKU-000003", 1}}},
        {"tx-b", {{"SKU-000001", 1}}},
    }));

    REQUIRE(applied == 2);
    REQUIRE(qty(db, "SKU-000001") == 0);
    REQUIRE(qty(db, "SKU-000003") == 2);
    REQUIRE(qty(db, "SKU-000002") == 3);
}

TEST_CASE("replaying a batch changes nothing (at-least-once delivery)", "[db]") {
    TinyDb db("inventory");
    Stock stock(db.client, 2);
    std::vector<SaleEvent> batch{{"tx-a", {{"SKU-000002", 1}}}};

    REQUIRE(drogon::sync_wait(stock.applySales(batch)) == 1);
    REQUIRE(drogon::sync_wait(stock.applySales(batch)) == 0);
    REQUIRE(qty(db, "SKU-000002") == 2);

    // A batch mixing an old and a new sale applies only the new one.
    batch.push_back({"tx-b", {{"SKU-000002", 1}}});
    REQUIRE(drogon::sync_wait(stock.applySales(batch)) == 1);
    REQUIRE(qty(db, "SKU-000002") == 1);
    REQUIRE(db.scalar<int>("SELECT count(*) FROM applied_sales") == 2);
}

TEST_CASE("stock floors at zero", "[db]") {
    TinyDb db("inventory");
    Stock stock(db.client, 2);

    drogon::sync_wait(stock.applySales({{"tx-b", {{"SKU-000002", 5}}}}));

    REQUIRE(qty(db, "SKU-000002") == 0);
}

TEST_CASE("low_stock_since is set once, on the first sale that crosses the threshold", "[db]") {
    TinyDb db("inventory");
    Stock stock(db.client, 2);

    REQUIRE(db.client->execSqlSync("SELECT 1 FROM stock WHERE sku = 'SKU-000001' AND low_stock_since IS NULL").size() == 1);
    drogon::sync_wait(stock.applySales({{"tx-c1", {{"SKU-000001", 2}}}}));   // 3 -> 1, crosses threshold 2
    auto first = db.scalar<std::string>("SELECT low_stock_since::text FROM stock WHERE sku = 'SKU-000001'");
    REQUIRE_FALSE(first.empty());

    drogon::sync_wait(stock.applySales({{"tx-c2", {{"SKU-000001", 1}}}}));   // 1 -> 0, already low
    auto second = db.scalar<std::string>("SELECT low_stock_since::text FROM stock WHERE sku = 'SKU-000001'");
    REQUIRE(second == first);
}

TEST_CASE("lowStock lists exactly the SKUs under the threshold, lowest first", "[db]") {
    TinyDb db("inventory");
    Stock stock(db.client, 2);
    drogon::sync_wait(stock.applySales({{"tx-e", {{"SKU-000001", 2}, {"SKU-000002", 5}, {"SKU-000003", 1}}}}));
    // quantities now: 1:1  2:0  3:2  4:3  5:3

    auto rows = drogon::sync_wait(stock.lowStock(3));

    REQUIRE(rows.size() == 3);
    REQUIRE(rows[0].sku == "SKU-000002");
    REQUIRE(rows[0].quantity == 0);
    REQUIRE(rows[0].since.has_value());
    REQUIRE(rows[1].sku == "SKU-000001");
    REQUIRE(rows[2].sku == "SKU-000003");
    REQUIRE_FALSE(rows[2].since.has_value());   // 2 is not < configured threshold 2
}
