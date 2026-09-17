#pragma once

#include <cstdint>
#include <string>
#include <vector>
#include <optional>
#include <drogon/orm/DbClient.h>
#include <drogon/utils/coroutine.h>

struct Basket; // reference to Basket in transactions.hpp

struct SaleLine { std::string sku; int32_t quantity; int64_t price_cents; };

struct SaleRow { 
    std::string stationId; 
    std::string startedAt; 
    int32_t itemCount; 
    int64_t totalPriceCents;
};

struct LowStockRow { 
    std::string sku; 
    std::string name; 
    int32_t quantity;
    std::optional<std::string> since;
};

class Inventory {
public:
    Inventory(drogon::orm::DbClientPtr db, int lowStockThreshold);
    // lines sorted by sku, no duplicates
    drogon::Task<std::string> completeSale(const Basket& basket, 
        const std::vector<SaleLine>& lines, int32_t itemCount, int64_t totalPriceCents);
    drogon::Task<std::optional<SaleRow>> findSale(std::string txId);
    drogon::Task<std::vector<LowStockRow>> lowStock(int threshold);
private:
    drogon::orm::DbClientPtr db_;
    int lowStockThreshold_;
};