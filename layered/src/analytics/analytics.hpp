#pragma once

#include <cstddef>
#include <cstdint>
#include <drogon/utils/coroutine.h>
#include <json/json.h>

#include "analytics/popularity_window.hpp"

class Catalog;
class Snapshots;

// Wraps the in-memory sliding window with the persisted-snapshot reads/writes it needs.
class Analytics {
public:
    Analytics(size_t windowSize, size_t slideInterval, size_t topN, size_t catalogSize);

    // One scan; persists a new snapshot when the window slides.
    void recordScan(uint32_t itemIndex, Snapshots& snapshots, const Catalog& catalog);

    // The /analytics/popular-items response body: latest persisted snapshot, capped at `limit` items.
    drogon::Task<Json::Value> popularItemsJson(int limit, Snapshots& snapshots);
private:
    PopularityWindow window_;
    size_t windowSize_, slideInterval_;
};
