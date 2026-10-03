#include "db/snapshots.hpp"

#include "analytics/filters.hpp"

Snapshots::Snapshots(drogon::orm::DbClientPtr db) : db_(std::move(db)) {}

void Snapshots::insert(const std::vector<SnapshotRow>& rows) {
    // The batch travels as one jsonb array, so the statement has one parameter whatever its size.
    Json::Value batch(Json::arrayValue);
    for (const auto& row : rows) {
        Json::Value r;
        r["windowStart"] = static_cast<Json::UInt64>(row.windowStart);
        r["windowEnd"] = static_cast<Json::UInt64>(row.windowEnd);
        r["computedAt"] = row.computedAt;
        r["items"] = row.items;
        batch.append(std::move(r));
    }
    Json::StreamWriterBuilder compact;
    compact["indentation"] = "";

    db_->execSqlSync(
        "INSERT INTO popular_snapshots (window_start, window_end, computed_at, items) "
        "SELECT (r->>'windowStart')::bigint, (r->>'windowEnd')::bigint, (r->>'computedAt')::timestamptz, r->'items' "
        "FROM jsonb_array_elements($1::jsonb) WITH ORDINALITY AS b(r, n) ORDER BY n",
        Json::writeString(compact, batch));
}

drogon::Task<std::optional<StoredSnapshot>> Snapshots::latest() {
    auto result = co_await db_->execSqlCoro(
        "SELECT window_start, window_end, iso8601(computed_at) AS computed_at, items::text "
        "FROM popular_snapshots ORDER BY id DESC LIMIT 1");
    if (result.empty()) co_return std::nullopt;
    const auto& row = result[0];

    Json::Value items;
    Json::CharReaderBuilder reader;
    std::string errs;
    const std::string text = row["items"].as<std::string>();
    std::unique_ptr<Json::CharReader>(reader.newCharReader())->parse(text.data(), text.data() + text.size(), &items, &errs);

    co_return StoredSnapshot{
        .windowStart = row["window_start"].as<uint64_t>(),
        .windowEnd = row["window_end"].as<uint64_t>(),
        .computedAt = row["computed_at"].as<std::string>(),
        .items = std::move(items),
    };
}
