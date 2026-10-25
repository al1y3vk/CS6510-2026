#pragma once

#include <atomic>

#include "api_error.hpp"
#include "app_config.hpp"
#include "catalog.hpp"
#include "db/stock.hpp"

// Inventory service: GET /inventory/low-stock, plus POST /events/sales (internal, from checkout's
// outbox relay). Owns stock in its own DB.
struct Services {
    AppConfig config;
    Catalog catalog;   // fetched from the catalog service at startup; only for item names
    Stock stock;
};

inline std::atomic<Services*> gServices{nullptr};
