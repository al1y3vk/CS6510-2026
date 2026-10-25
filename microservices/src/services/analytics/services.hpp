#pragma once

#include <atomic>

#include "analytics/analytics.hpp"
#include "api_error.hpp"
#include "app_config.hpp"
#include "catalog.hpp"
#include "db/snapshots.hpp"

// Analytics service: GET /analytics/popular-items, plus POST /events/scans (internal, from
// checkout). Scans feed the window → rank → format → persist pipeline; reads come from
// popular_snapshots in its own DB.
struct Services {
    AppConfig config;
    Catalog catalog;   // fetched from the catalog service at startup
    Analytics analytics;
    Snapshots snapshots;
};

inline std::atomic<Services*> gServices{nullptr};
