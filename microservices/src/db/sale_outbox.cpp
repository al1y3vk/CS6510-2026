#include "db/sale_outbox.hpp"

#include "money.hpp"

SaleOutbox::SaleOutbox(drogon::orm::DbClientPtr db) : db_(std::move(db)) {}

OutboxBatch SaleOutbox::pending(size_t limit) {
    auto result = db_->execSqlSync(
        "SELECT o.id, o.transaction_id, r.sku, r.quantity "
        "FROM (SELECT id, transaction_id FROM sale_outbox ORDER BY id LIMIT $1) o "
        "JOIN receipt_lines r USING (transaction_id) ORDER BY o.id, r.sku",
        static_cast<int64_t>(limit));
    OutboxBatch batch;
    for (const auto& row : result) {
        const auto id = row["id"].as<int64_t>();
        if (batch.ids.empty() || batch.ids.back() != id) {
            batch.ids.push_back(id);
            batch.sales.push_back({.transactionId = row["transaction_id"].as<std::string>(), .lines = {}});
        }
        batch.sales.back().lines.push_back({row["sku"].as<std::string>(), row["quantity"].as<int32_t>()});
    }
    return batch;
}

void SaleOutbox::remove(const std::vector<int64_t>& ids) {
    // By id, not "id <= max": a sale with a lower id may commit after this batch was read.
    db_->execSqlSync("DELETE FROM sale_outbox WHERE id = ANY($1::bigint[])", pgArray(ids));
}
