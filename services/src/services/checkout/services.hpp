#pragma once

#include <atomic>
#include <thread>

#include "analytics/pipe.hpp"
#include "api_error.hpp"
#include "app_config.hpp"
#include "catalog.hpp"
#include "db/sales.hpp"
#include "db/scan_log.hpp"
#include "transactions/transactions.hpp"

// Checkout service: /transactions/**. Owns open baskets (RAM) and completed sales (DB).
struct Services {
    AppConfig config;
    Catalog catalog;
    Transactions transactions;
    Sales sales;
    ScanLog scanLog;
    Pipe<uint32_t> scans;     // scan handler pushes item indexes; `publisher` drains them into scan_events
    std::jthread publisher;
};

inline std::atomic<Services*> gServices{nullptr};
