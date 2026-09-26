#include "analytics/analytics.hpp"

#include <chrono>

#include "db/snapshots.hpp"
#include "money.hpp"

Analytics::Analytics(size_t windowSize, size_t slideInterval, size_t topN, size_t catalogSize)
    : window_(windowSize, slideInterval, topN, catalogSize), windowSize_(windowSize), slideInterval_(slideInterval) {}

void Analytics::recordScan(uint32_t itemIndex, Snapshots& snapshots, const Catalog& catalog) {
    if (auto snap = window_.record(itemIndex)) snapshots.insertAsync(*snap, catalog);
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
