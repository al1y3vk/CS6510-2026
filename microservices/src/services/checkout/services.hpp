#pragma once

#include <atomic>
#include <thread>

#include "analytics/pipe.hpp"
#include "api_error.hpp"
#include "app_config.hpp"
#include "catalog.hpp"
#include "db/sale_outbox.hpp"
#include "db/sales.hpp"
#include "http/peer.hpp"
#include "transactions/transactions.hpp"

// Checkout service: /transactions/**. Owns open baskets (RAM) and completed sales (its own DB).
struct Services {
    AppConfig config;
    Catalog catalog;          // fetched from the catalog service at startup
    Transactions transactions;
    Sales sales;
    SaleOutbox outbox;
    Peer inventory;           // `relay` sends sale_outbox rows here
    Peer analytics;           // `publisher` sends scans here
    Pipe<uint32_t> scans;     // scan handler pushes item indexes; `publisher` drains them
    std::jthread publisher;
    std::jthread relay;
};

inline std::atomic<Services*> gServices{nullptr};
