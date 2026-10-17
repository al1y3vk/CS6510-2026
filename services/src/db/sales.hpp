#pragma once

#include <cstdint>
#include <string>
#include <vector>
#include <optional>
#include <drogon/orm/DbClient.h>
#include <drogon/utils/coroutine.h>

#include "transactions/transactions.hpp"   // Basket, SaleLine

struct SaleRow {
    std::string stationId;
    std::string startedAt;
    int32_t itemCount;
    int64_t totalPriceCents;
};

// sales + receipt_lines (writes), stock (decrement inside complete_sale). Checkout service only.
class Sales {
public:
    Sales(drogon::orm::DbClientPtr db, int lowStockThreshold);
    // lines sorted by sku, no duplicates
    drogon::Task<std::string> completeSale(const Basket& basket,
        const std::vector<SaleLine>& lines, int32_t itemCount, int64_t totalPriceCents);
    drogon::Task<std::optional<SaleRow>> findSale(std::string txId);
private:
    drogon::orm::DbClientPtr db_;
    int lowStockThreshold_;
};
