#include "db/stock.hpp"

Stock::Stock(drogon::orm::DbClientPtr db) : db_(std::move(db)) {}

drogon::Task<std::vector<LowStockRow>> Stock::lowStock(int threshold) {
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
