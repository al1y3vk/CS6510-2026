#pragma once

#include <atomic>
#include <thread>

#include "api_error.hpp"
#include "app_config.hpp"
#include "catalog.hpp"
#include "db/stock.hpp"

// Inventory service: GET /inventory/low-stock, plus a consumer thread that applies sales.completed
// events to stock. Owns stock in its own DB.
struct Services {
    AppConfig config;
    Catalog catalog;   // the newest catalog.items event, read at startup; only for item names
    Stock stock;
    std::jthread consumer;
};

inline std::atomic<Services*> gServices{nullptr};
