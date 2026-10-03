#include "analytics/analytics.hpp"

#include <chrono>
#include <trantor/utils/Logger.h>

#include "db/snapshots.hpp"
#include "money.hpp"

Analytics::Analytics(size_t windowSize, size_t slideInterval, size_t topN)
    : windowSize_(windowSize), slideInterval_(slideInterval), topN_(topN) {}

void Analytics::start(const Catalog& catalog, Snapshots& snapshots) {
    pipeline_ = std::make_unique<AnalyticsPipeline>(windowSize_, slideInterval_, topN_, catalog,
        [&snapshots](const std::vector<SnapshotRow>& rows) {
            try {
                snapshots.insert(rows);
            } catch (const drogon::orm::DrogonDbException& e) {
                LOG_ERROR << "snapshot insert (" << rows.size() << " rows, last windowEnd " << rows.back().windowEnd
                          << ") failed: " << e.base().what();
            }
        });
}

drogon::Task<Json::Value> Analytics::popularItemsJson(int limit, Snapshots& snapshots) {
    auto snap = co_await snapshots.latest();

    Json::Value body;
    body["windowSize"] = static_cast<Json::UInt64>(windowSize_);
    body["slideInterval"] = static_cast<Json::UInt64>(slideInterval_);
    Json::Value items(Json::arrayValue);
    if (snap) {
        body["windowStart"] = static_cast<Json::UInt64>(snap->windowStart);
        body["windowEnd"] = static_cast<Json::UInt64>(snap->windowEnd);
        body["computedAt"] = snap->computedAt;
        for (Json::ArrayIndex i = 0; i < snap->items.size() && static_cast<int>(i) < limit; ++i) items.append(snap->items[i]);
    } else {
        body["windowStart"] = 0;
        body["windowEnd"] = 0;
        body["computedAt"] = rfc3339(std::chrono::system_clock::now());
    }
    body["items"] = std::move(items);
    co_return body;
}
