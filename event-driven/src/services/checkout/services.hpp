#pragma once

#include <atomic>
#include <thread>

#include "api_error.hpp"
#include "app_config.hpp"
#include "bus/bus.hpp"
#include "catalog.hpp"
#include "db/sale_outbox.hpp"
#include "db/sales.hpp"
#include "transactions/transactions.hpp"

// Checkout service: /transactions/**. Owns open baskets (RAM) and completed sales (its own DB).
struct Services {
    AppConfig config;
    Catalog catalog;          // the newest catalog.items event, read at startup
    Transactions transactions;
    Sales sales;
    SaleOutbox outbox;
    Producer scanEvents;      // scan handler publishes items.scanned here
    Producer saleEvents;      // `relay` publishes sale_outbox rows to sales.completed here
    std::jthread relay;
};

inline std::atomic<Services*> gServices{nullptr};
