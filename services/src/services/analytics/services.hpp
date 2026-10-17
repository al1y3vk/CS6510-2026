#pragma once

#include <atomic>
#include <thread>

#include "analytics/analytics.hpp"
#include "api_error.hpp"
#include "app_config.hpp"
#include "catalog.hpp"
#include "db/scan_log.hpp"
#include "db/snapshots.hpp"

// Analytics service: GET /analytics/popular-items. `poller` tails scan_events into the
// window → rank → format → persist pipeline; reads come from popular_snapshots.
struct Services {
    AppConfig config;
    Catalog catalog;
    Analytics analytics;
    Snapshots snapshots;
    ScanLog scanLog;
    std::jthread poller;
};

inline std::atomic<Services*> gServices{nullptr};
