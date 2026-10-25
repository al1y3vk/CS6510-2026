#pragma once

#include <cstdint>
#include <vector>
#include <drogon/orm/DbClient.h>

#include "sale_event.hpp"

struct OutboxBatch {
    std::vector<int64_t> ids;       // sale_outbox ids, to remove once inventory has applied the batch
    std::vector<SaleEvent> sales;
};

// sale_outbox (read + delete), checkout's database. Both calls block; only the relay thread uses them.
class SaleOutbox {
public:
    explicit SaleOutbox(drogon::orm::DbClientPtr db);
    // Up to `limit` pending sales, oldest first, each with its receipt lines.
    OutboxBatch pending(size_t limit);
    void remove(const std::vector<int64_t>& ids);
private:
    drogon::orm::DbClientPtr db_;
};
