#include "db/inventory.hpp"

#include "money.hpp"

Inventory::Inventory(drogon::orm::DbClientPtr db, int lowStockThreshold)
    : db_(std::move(db)), lowStockThreshold_(lowStockThreshold) {}

drogon::Task<std::string> Inventory::completeSale(const Basket& basket, const std::vector<SaleLine>& lines,
                                                  int32_t itemCount, int64_t totalPriceCents) {
    std::vector<std::string> skus;
    std::vector<int64_t> qtys, cents;
    for (const SaleLine& l : lines) {
        skus.push_back(l.sku);
        qtys.push_back(l.quantity);
        cents.push_back(l.price_cents);
    }
    auto result = co_await db_->execSqlCoro(
        "SELECT iso8601(complete_sale($1, $2, $3::timestamptz, $4::text[], $5::int[], $6::int[], "
        "$7::int, $8::bigint, $9::int))",
        basket.id, basket.stationId, rfc3339(basket.startedAt), pgArray(skus), pgArray(qtys), pgArray(cents),
        itemCount, totalPriceCents, lowStockThreshold_);
    co_return result[0][0].as<std::string>();
}

drogon::Task<std::optional<SaleRow>> Inventory::findSale(std::string txId) {
    auto result = co_await db_->execSqlCoro(
        "SELECT station_id, iso8601(started_at) AS started_at, item_count, total_cents "
        "FROM sales WHERE transaction_id = $1",
        txId);
    if (result.empty()) co_return std::nullopt;
    const auto& row = result[0];
    co_return SaleRow{
        .stationId = row["station_id"].as<std::string>(),
        .startedAt = row["started_at"].as<std::string>(),
        .itemCount = row["item_count"].as<int32_t>(),
        .totalPriceCents = row["total_cents"].as<int64_t>(),
    };
}

drogon::Task<std::vector<LowStockRow>> Inventory::lowStock(int threshold) {
    auto result = co_await db_->execSqlCoro(
        "SELECT s.sku, i.name, s.quantity, iso8601(s.low_stock_since) AS since "
        "FROM stock s JOIN items i USING (sku) WHERE s.quantity < $1::int ORDER BY s.quantity, s.sku",
        threshold);
    std::vector<LowStockRow> rows;
    rows.reserve(result.size());
    for (const auto& row : result) {
        rows.push_back({
            .sku = row["sku"].as<std::string>(),
            .name = row["name"].as<std::string>(),
            .quantity = row["quantity"].as<int32_t>(),
            .since = row["since"].isNull() ? std::nullopt : std::optional(row["since"].as<std::string>()),
        });
    }
    co_return rows;
}
