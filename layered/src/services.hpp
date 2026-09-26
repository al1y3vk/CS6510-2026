#pragma once

#include <atomic>
#include <drogon/HttpResponse.h>

#include "analytics/analytics.hpp"
#include "api_error.hpp"
#include "app_config.hpp"
#include "catalog.hpp"
#include "db/inventory.hpp"
#include "db/snapshots.hpp"
#include "transactions/transactions.hpp"

// Everything the controllers need. Built once in main.cpp's beginning advice (after the
// DB client exists) and never destroyed; Drogon constructs controllers itself, so they
// reach it through gServices instead of constructor injection.
struct Services {
    AppConfig config;
    Catalog catalog;
    Transactions transactions;
    Analytics analytics;
    Inventory inventory;
    Snapshots snapshots;
};

inline std::atomic<Services*> gServices{nullptr};

// The listener can accept a request in the few ms before the advice has run.
inline drogon::HttpResponsePtr notReady() {
    return apiError(drogon::k503ServiceUnavailable, "STARTING", "catalog not loaded yet");
}
