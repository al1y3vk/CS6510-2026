#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_string.hpp>
#include <drogon/utils/coroutine.h>

#include "db/sale_outbox.hpp"
#include "db/sales.hpp"
#include "db_fixture.hpp"
#include "transactions/transactions.hpp"

using Catch::Matchers::Matches;

namespace {
constexpr auto kIso8601 = R"(\d{4}-\d{2}-\d{2}T\d{2}:\d{2}:\d{2}\.\d{3}Z)";

std::string sell(Sales& sales, const std::string& txId, std::vector<SaleLine> lines) {
    Basket b;   // holds a mutex, so it is built in place rather than returned from a helper
    b.id = txId;
    b.stationId = "station-1";
    b.startedAt = std::chrono::system_clock::now();
    int32_t count = 0;
    int64_t total = 0;
    for (const auto& l : lines) { count += l.quantity; total += l.quantity * l.price_cents; }
    return drogon::sync_wait(sales.completeSale(b, lines, count, total));
}
}  // namespace

TEST_CASE("completeSale records the sale, its lines and one outbox row", "[db]") {
    TinyDb db("checkout");
    Sales sales(db.client);

    std::string completedAt = sell(sales, "tx-a", {{"SKU-000001", 2, 85}, {"SKU-000003", 1, 155}});

    REQUIRE_THAT(completedAt, Matches(kIso8601));
    REQUIRE(db.scalar<int>("SELECT item_count FROM sales WHERE transaction_id = 'tx-a'") == 3);
    REQUIRE(db.scalar<long>("SELECT total_cents FROM sales WHERE transaction_id = 'tx-a'") == 325);
    REQUIRE(db.scalar<int>("SELECT count(*) FROM receipt_lines WHERE transaction_id = 'tx-a'") == 2);
    REQUIRE(db.scalar<int>("SELECT count(*) FROM sale_outbox WHERE transaction_id = 'tx-a'") == 1);
}

TEST_CASE("findSale is empty before completion and returns the row after", "[db]") {
    TinyDb db("checkout");
    Sales sales(db.client);

    REQUIRE_FALSE(drogon::sync_wait(sales.findSale("tx-d")).has_value());
    sell(sales, "tx-d", {{"SKU-000004", 1, 190}, {"SKU-000005", 2, 225}});

    auto row = drogon::sync_wait(sales.findSale("tx-d"));
    REQUIRE(row.has_value());
    REQUIRE(row->stationId == "station-1");
    REQUIRE_THAT(row->startedAt, Matches(kIso8601));
    REQUIRE(row->itemCount == 3);
    REQUIRE(row->totalPriceCents == 640);
}

TEST_CASE("outbox pending returns sales oldest first with their lines, capped at limit", "[db]") {
    TinyDb db("checkout");
    Sales sales(db.client);
    SaleOutbox outbox(db.client);
    sell(sales, "tx-1", {{"SKU-000001", 2, 85}, {"SKU-000002", 1, 120}});
    sell(sales, "tx-2", {{"SKU-000003", 4, 155}});
    sell(sales, "tx-3", {{"SKU-000001", 1, 85}});

    auto batch = outbox.pending(2);

    REQUIRE(batch.ids.size() == 2);
    REQUIRE(batch.sales.size() == 2);
    REQUIRE(batch.sales[0].transactionId == "tx-1");
    REQUIRE(batch.sales[0].lines.size() == 2);
    REQUIRE(batch.sales[0].lines[0].sku == "SKU-000001");
    REQUIRE(batch.sales[0].lines[0].quantity == 2);
    REQUIRE(batch.sales[1].transactionId == "tx-2");
    REQUIRE(batch.sales[1].lines[0].quantity == 4);
}

TEST_CASE("outbox remove deletes exactly the sent rows", "[db]") {
    TinyDb db("checkout");
    Sales sales(db.client);
    SaleOutbox outbox(db.client);
    sell(sales, "tx-1", {{"SKU-000001", 1, 85}});
    sell(sales, "tx-2", {{"SKU-000002", 1, 120}});

    auto first = outbox.pending(1);
    outbox.remove(first.ids);

    auto rest = outbox.pending(10);
    REQUIRE(rest.sales.size() == 1);
    REQUIRE(rest.sales[0].transactionId == "tx-2");
    REQUIRE(db.scalar<int>("SELECT count(*) FROM sales") == 2);   // the sale itself stays
    outbox.remove(rest.ids);
    REQUIRE(outbox.pending(10).ids.empty());
}
