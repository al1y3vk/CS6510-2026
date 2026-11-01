#pragma once

#include <atomic>
#include <thread>

#include "analytics/analytics.hpp"
#include "api_error.hpp"
#include "app_config.hpp"
#include "catalog.hpp"
#include "db/snapshots.hpp"

// Analytics service: GET /analytics/popular-items, plus a consumer thread that feeds items.scanned
// events into the window → rank → format → persist pipeline. Reads come from popular_snapshots
// in its own DB.
struct Services {
    AppConfig config;
    Catalog catalog;   // the newest catalog.items event, read at startup
    Analytics analytics;
    Snapshots snapshots;
    std::jthread consumer;
};

inline std::atomic<Services*> gServices{nullptr};
