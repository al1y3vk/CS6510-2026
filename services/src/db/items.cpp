#include "db/items.hpp"

#include <stdexcept>

Catalog loadCatalog(const drogon::orm::DbClientPtr& db) {
    auto rows = db->execSqlSync("SELECT sku, id, name, price_cents FROM items ORDER BY id");
    if (rows.empty()) throw std::runtime_error("catalog is empty — run services/db/reset.sh");
    std::vector<Item> items;
    items.reserve(rows.size());
    for (const auto& row : rows) {
        items.push_back({row["id"].as<uint32_t>() - 1, row["sku"].as<std::string>(),
                         row["name"].as<std::string>(), row["price_cents"].as<int64_t>()});
    }
    return Catalog{std::move(items)};
}
