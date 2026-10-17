#include "db/scan_log.hpp"

#include "money.hpp"

ScanLog::ScanLog(drogon::orm::DbClientPtr db) : db_(std::move(db)) {}

void ScanLog::append(const std::vector<uint32_t>& itemIndices) {
    db_->execSqlSync(
        "INSERT INTO scan_events (item_index) "
        "SELECT i FROM unnest($1::int[]) WITH ORDINALITY AS b(i, n) ORDER BY n",
        pgArray(std::vector<int64_t>(itemIndices.begin(), itemIndices.end())));
}

std::vector<ScanEvent> ScanLog::after(int64_t afterId, size_t limit) {
    auto result = db_->execSqlSync(
        "SELECT id, item_index FROM scan_events WHERE id > $1 ORDER BY id LIMIT $2",
        afterId, static_cast<int64_t>(limit));
    std::vector<ScanEvent> events;
    events.reserve(result.size());
    for (const auto& row : result) events.push_back({row["id"].as<int64_t>(), row["item_index"].as<uint32_t>()});
    return events;
}
