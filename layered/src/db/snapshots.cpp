#include "db/snapshots.hpp"

#include <trantor/utils/Logger.h>

#include "analytics/popularity_window.hpp"
#include "catalog.hpp"
#include "money.hpp"

Snapshots::Snapshots(drogon::orm::DbClientPtr db) : db_(std::move(db)) {}

void Snapshots::insertAsync(const Snapshot& snapshot, const Catalog& catalog) {
    Json::Value items(Json::arrayValue);
    for (size_t rank = 0; rank < snapshot.top.size(); ++rank) {
        const Item& item = catalog.at(snapshot.top[rank].index);
        Json::Value entry;
        entry["sku"] = item.sku;
        entry["name"] = item.name;
        entry["scanCount"] = snapshot.top[rank].count;
        entry["rank"] = static_cast<Json::UInt>(rank + 1);
        items.append(std::move(entry));
    }
    Json::StreamWriterBuilder compact;
    compact["indentation"] = "";

    db_->execSqlAsync(
        "INSERT INTO popular_snapshots (window_start, window_end, computed_at, items) "
        "VALUES ($1::bigint, $2::bigint, $3::timestamptz, $4::jsonb)",
        [](const drogon::orm::Result&) {},
        [end = snapshot.windowEnd](const drogon::orm::DrogonDbException& e) {
            LOG_ERROR << "snapshot insert (windowEnd " << end << ") failed: " << e.base().what();
        },
        static_cast<int64_t>(snapshot.windowStart), static_cast<int64_t>(snapshot.windowEnd),
        rfc3339(snapshot.computedAt), Json::writeString(compact, items));
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
