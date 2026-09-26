#include "catalog.hpp"

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
