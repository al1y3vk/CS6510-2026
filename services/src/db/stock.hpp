#pragma once

#include <cstdint>
#include <string>
#include <vector>
#include <optional>
#include <drogon/orm/DbClient.h>
#include <drogon/utils/coroutine.h>

struct LowStockRow {
    std::string sku;
    std::string name;
    int32_t quantity;
    std::optional<std::string> since;
};

// stock + items (reads). Inventory service only; checkout writes stock through complete_sale.
class Stock {
public:
    explicit Stock(drogon::orm::DbClientPtr db);
    drogon::Task<std::vector<LowStockRow>> lowStock(int threshold);
private:
    drogon::orm::DbClientPtr db_;
};
