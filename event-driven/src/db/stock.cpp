#include "db/stock.hpp"

#include "money.hpp"

Stock::Stock(drogon::orm::DbClientPtr db, int lowStockThreshold)
    : db_(std::move(db)), lowStockThreshold_(lowStockThreshold) {}

drogon::Task<std::vector<LowStockRow>> Stock::lowStock(int threshold) {
    auto result = co_await db_->execSqlCoro(
        "SELECT sku, quantity, iso8601(low_stock_since) AS since "
        "FROM stock WHERE quantity < $1::int ORDER BY quantity, sku",
        threshold);
    std::vector<LowStockRow> rows;
    rows.reserve(result.size());
    for (const auto& row : result) {
        rows.push_back({
            .sku = row["sku"].as<std::string>(),
            .quantity = row["quantity"].as<int32_t>(),
            .since = row["since"].isNull() ? std::nullopt : std::optional(row["since"].as<std::string>()),
        });
    }
    co_return rows;
}

drogon::Task<int> Stock::applySales(const std::vector<SaleEvent>& sales) {
    std::vector<std::string> txIds, lineTx, skus;
    std::vector<int64_t> qtys;
    for (const SaleEvent& sale : sales) {
        txIds.push_back(sale.transactionId);
        for (const SaleEvent::Line& l : sale.lines) {
            lineTx.push_back(sale.transactionId);
            skus.push_back(l.sku);
            qtys.push_back(l.quantity);
        }
    }
    auto result = co_await db_->execSqlCoro(
        "SELECT apply_sales($1::text[], $2::text[], $3::text[], $4::int[], $5::int)",
        pgArray(txIds), pgArray(lineTx), pgArray(skus), pgArray(qtys), lowStockThreshold_);
    co_return result[0][0].as<int>();
}
