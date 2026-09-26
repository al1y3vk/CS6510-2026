#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_string.hpp>
#include <drogon/utils/coroutine.h>

#include "db/inventory.hpp"
#include "db_fixture.hpp"
#include "transactions/transactions.hpp"

using Catch::Matchers::Matches;

namespace {
constexpr auto kIso8601 = R"(\d{4}-\d{2}-\d{2}T\d{2}:\d{2}:\d{2}\.\d{3}Z)";

std::string sell(Inventory& inv, const std::string& txId, std::vector<SaleLine> lines) {
    Basket b;   // holds a mutex, so it is built in place rather than returned from a helper
    b.id = txId;
    b.stationId = "station-1";
    b.startedAt = std::chrono::system_clock::now();
    int32_t count = 0;
    int64_t total = 0;
    for (const auto& l : lines) { count += l.quantity; total += l.quantity * l.price_cents; }
    return drogon::sync_wait(inv.completeSale(b, lines, count, total));
}
}  // namespace

TEST_CASE("completeSale decrements stock and records the sale", "[db]") {
    TinyDb db;
    Inventory inv(db.client, 2);

    std::string completedAt = sell(inv, "tx-a", {{"SKU-000001", 2, 85}, {"SKU-000003", 1, 155}});

    REQUIRE_THAT(completedAt, Matches(kIso8601));
    REQUIRE(db.scalar<int>("SELECT quantity FROM stock WHERE sku = 'SKU-000001'") == 1);
    REQUIRE(db.scalar<int>("SELECT quantity FROM stock WHERE sku = 'SKU-000003'") == 2);
    REQUIRE(db.scalar<int>("SELECT item_count FROM sales WHERE transaction_id = 'tx-a'") == 3);
    REQUIRE(db.scalar<long>("SELECT total_cents FROM sales WHERE transaction_id = 'tx-a'") == 325);
    REQUIRE(db.scalar<int>("SELECT count(*) FROM receipt_lines WHERE transaction_id = 'tx-a'") == 2);
}

TEST_CASE("stock floors at zero but the receipt keeps the full quantity", "[db]") {
    TinyDb db;
    Inventory inv(db.client, 2);

    sell(inv, "tx-b", {{"SKU-000002", 5, 120}});

    REQUIRE(db.scalar<int>("SELECT quantity FROM stock WHERE sku = 'SKU-000002'") == 0);
    REQUIRE(db.scalar<int>("SELECT quantity FROM receipt_lines WHERE transaction_id = 'tx-b'") == 5);
}

TEST_CASE("low_stock_since is set once, on the first sale that crosses the threshold", "[db]") {
    TinyDb db;
    Inventory inv(db.client, 2);

    REQUIRE(db.client->execSqlSync("SELECT 1 FROM stock WHERE sku = 'SKU-000001' AND low_stock_since IS NULL").size() == 1);
    sell(inv, "tx-c1", {{"SKU-000001", 2, 85}});   // 3 -> 1, crosses threshold 2
    auto first = db.scalar<std::string>("SELECT low_stock_since::text FROM stock WHERE sku = 'SKU-000001'");
    REQUIRE_FALSE(first.empty());

    sell(inv, "tx-c2", {{"SKU-000001", 1, 85}});   // 1 -> 0, already low
    auto second = db.scalar<std::string>("SELECT low_stock_since::text FROM stock WHERE sku = 'SKU-000001'");
    REQUIRE(second == first);
}

TEST_CASE("findSale is empty before completion and returns the row after", "[db]") {
    TinyDb db;
    Inventory inv(db.client, 2);

    REQUIRE_FALSE(drogon::sync_wait(inv.findSale("tx-d")).has_value());
    sell(inv, "tx-d", {{"SKU-000004", 1, 190}, {"SKU-000005", 2, 225}});

    auto row = drogon::sync_wait(inv.findSale("tx-d"));
    REQUIRE(row.has_value());
    REQUIRE(row->stationId == "station-1");
    REQUIRE_THAT(row->startedAt, Matches(kIso8601));
    REQUIRE(row->itemCount == 3);
    REQUIRE(row->totalPriceCents == 640);
}

TEST_CASE("lowStock lists exactly the SKUs under the threshold, lowest first", "[db]") {
    TinyDb db;
    Inventory inv(db.client, 2);
    sell(inv, "tx-e", {{"SKU-000001", 2, 85}, {"SKU-000002", 5, 120}, {"SKU-000003", 1, 155}});
    // quantities now: 1:1  2:0  3:2  4:3  5:3

    auto rows = drogon::sync_wait(inv.lowStock(3));

    REQUIRE(rows.size() == 3);
    REQUIRE(rows[0].sku == "SKU-000002");
    REQUIRE(rows[0].quantity == 0);
    REQUIRE(rows[0].name == "Item 2");
    REQUIRE(rows[0].since.has_value());
    REQUIRE(rows[1].sku == "SKU-000001");
    REQUIRE(rows[2].sku == "SKU-000003");
    REQUIRE_FALSE(rows[2].since.has_value());   // 2 is not < configured threshold 2
}

TEST_CASE("verify.sql finds no mismatch after mixed sales", "[db]") {
    TinyDb db;
    Inventory inv(db.client, 2);
    sell(inv, "tx-f1", {{"SKU-000001", 2, 85}, {"SKU-000002", 5, 120}});
    sell(inv, "tx-f2", {{"SKU-000001", 3, 85}});

    const std::string cmd = std::string("psql ") + pgUrl() + " -tA -f " + MONOLITH_DIR + "/db/verify.sql | grep -c . ; exit 0";
    // grep -c prints 0 when there are no rows; the exit 0 keeps the pipeline status simple.
    FILE* p = popen(cmd.c_str(), "r");
    char buf[16] = {0};
    if (!fgets(buf, sizeof buf, p)) buf[0] = 0;
    pclose(p);
    REQUIRE(std::string(buf) == "0\n");
}
