#pragma once

#include <cstddef>
#include <cstdint>
#include <drogon/utils/coroutine.h>
#include <json/json.h>
#include <memory>

#include "analytics/pipeline.hpp"

class Catalog;
class Snapshots;

// Front of the analytics pipeline (scans in) and the read side (latest persisted snapshot out).
class Analytics {
public:
    Analytics(size_t windowSize, size_t slideInterval, size_t topN);

    // Starts the filter threads. Call once, before the first recordScan; both must outlive this.
    void start(const Catalog& catalog, Snapshots& snapshots);

    // One scan; hands it to the pipeline and returns immediately.
    void recordScan(uint32_t itemIndex) { pipeline_->submit(itemIndex); }

    // The /analytics/popular-items response body: latest persisted snapshot, capped at `limit` items.
    drogon::Task<Json::Value> popularItemsJson(int limit, Snapshots& snapshots);
private:
    std::unique_ptr<AnalyticsPipeline> pipeline_;
    size_t windowSize_, slideInterval_, topN_;
};
