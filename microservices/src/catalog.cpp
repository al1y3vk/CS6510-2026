#include "catalog.hpp"

#include <cmath>
#include <stdexcept>

Catalog::Catalog(std::vector<Item> items) : items_(std::move(items)) {
    bySku_.reserve(items_.size());
    for (const Item& item : items_) bySku_.emplace(item.sku, item.index);
}

const Item& Catalog::at(uint32_t index) const { return items_[index]; }

const Item* Catalog::find(const std::string& sku) const {
    auto it = bySku_.find(sku);
    return it == bySku_.end() ? nullptr : &items_[it->second];
}

size_t Catalog::size() const { return items_.size(); }

const std::vector<Item>& Catalog::all() const { return items_; }

Catalog catalogFromJson(const Json::Value& body) {
    const Json::Value& items = body["items"];
    if (!items.isArray() || items.empty()) throw std::runtime_error("catalog response has no items");
    std::vector<Item> out;
    out.reserve(items.size());
    for (const Json::Value& i : items) {
        if (!i["sku"].isString() || !i["name"].isString() || !i["price"].isNumeric())
            throw std::runtime_error("catalog item needs sku, name and price");
        out.push_back({static_cast<uint32_t>(out.size()), i["sku"].asString(), i["name"].asString(),
                       std::llround(i["price"].asDouble() * 100)});
    }
    return Catalog{std::move(out)};
}
