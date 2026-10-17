#pragma once

#include <atomic>

#include "api_error.hpp"
#include "app_config.hpp"
#include "db/stock.hpp"

// Inventory service: GET /inventory/low-stock. Reads the stock checkout decrements.
struct Services {
    AppConfig config;
    Stock stock;
};

inline std::atomic<Services*> gServices{nullptr};
