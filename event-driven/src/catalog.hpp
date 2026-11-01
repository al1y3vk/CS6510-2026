#pragma once

#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>
#include <json/json.h>

struct Item {
    uint32_t index;
    std::string sku;
    std::string name;
    int64_t price_cents;
};

class Catalog {
public:
    explicit Catalog(std::vector<Item> items);
    const Item& at(uint32_t index) const;
    const Item* find(const std::string& sku) const;

    size_t size() const;
    const std::vector<Item>& all() const;
private:
    std::vector<Item> items_;
    std::unordered_map<std::string, uint32_t> bySku_;
};

// {"items": [{sku, name, price}, ...]}: the GET /items body and the catalog.items event value.
Json::Value catalogJson(const Catalog& catalog);
// The reverse, keeping the order as the item index. Throws std::runtime_error on an empty or
// malformed body.
Catalog catalogFromJson(const Json::Value& body);
