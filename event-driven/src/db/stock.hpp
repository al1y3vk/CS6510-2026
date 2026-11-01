#pragma once

#include <cstdint>
#include <string>
#include <vector>
#include <optional>
#include <drogon/orm/DbClient.h>
#include <drogon/utils/coroutine.h>

#include "events.hpp"

struct LowStockRow {
    std::string sku;
    int32_t quantity;
    std::optional<std::string> since;
};

// stock + applied_sales, in the inventory service's own database.
class Stock {
public:
    explicit Stock(drogon::orm::DbClientPtr db, int lowStockThreshold);
    drogon::Task<std::vector<LowStockRow>> lowStock(int threshold);
    // Decrements stock for every sale not applied before, all in one transaction. Replaying a
    // batch changes nothing. Returns how many sales were new.
    drogon::Task<int> applySales(const std::vector<SaleEvent>& sales);
private:
    drogon::orm::DbClientPtr db_;
    int lowStockThreshold_;
};
