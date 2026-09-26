#include "transactions/transactions.hpp"

#include <algorithm>
#include <random>

namespace {
// 128 random bits as 32 lowercase hex chars. checkout_core stays free of Drogon,
// so this stands in for drogon::utils::getUuid(); collisions are as unlikely.
std::string randomHex128() {
    thread_local std::mt19937_64 rng{std::random_device{}()};
    static constexpr char digits[] = "0123456789abcdef";
    std::string out;
    out.reserve(32);
    for (int word = 0; word < 2; ++word) {
        uint64_t bits = rng();
        for (int i = 0; i < 16; ++i, bits >>= 4) out += digits[bits & 0xF];
    }
    return out;
}
}  // namespace

std::shared_ptr<Basket> Transactions::start(std::string stationId) {
    auto basket = std::make_shared<Basket>();
    basket->id = "tx-" + randomHex128();
    basket->stationId = std::move(stationId);
    basket->startedAt = std::chrono::system_clock::now();

    std::unique_lock lock(mu_);
    open_.emplace(basket->id, basket);
    return basket;
}

std::shared_ptr<Basket> Transactions::find(const std::string& basketId) const {
    std::shared_lock lock(mu_);
    auto it = open_.find(basketId);
    return it == open_.end() ? nullptr : it->second;
}

void Transactions::remove(const std::string& basketId) {
    std::unique_lock lock(mu_);
    open_.erase(basketId);
}

size_t Transactions::openCount() const {
    std::shared_lock lock(mu_);
    return open_.size();
}

std::optional<ScanResult> Transactions::scan(Basket& basket, const Item& item) {
    std::lock_guard lock(basket.mu);
    if (basket.state != BasketState::Open) return std::nullopt;
    basket.itemIndices.push_back(item.index);
    basket.totalPriceCents += item.price_cents;
    return ScanResult{static_cast<int32_t>(basket.itemIndices.size()), basket.totalPriceCents};
}

PreparedSale Transactions::prepareComplete(Basket& basket, const Catalog& catalog) {
    PreparedSale sale;
    std::vector<uint32_t> indices;
    {
        std::lock_guard lock(basket.mu);
        if (basket.state != BasketState::Open) {
            sale.status = CompleteStatus::NotOpen;
            return sale;
        }
        if (basket.itemIndices.empty()) {
            sale.status = CompleteStatus::EmptyBasket;
            return sale;
        }
        basket.state = BasketState::Completing;
        indices = basket.itemIndices;
        sale.totalPriceCents = basket.totalPriceCents;
    }
    sale.itemCount = static_cast<int32_t>(indices.size());

    std::unordered_map<uint32_t, size_t> lineOf;
    for (uint32_t index : indices) {
        auto [it, inserted] = lineOf.try_emplace(index, sale.receipt.size());
        if (inserted) sale.receipt.push_back({index, 1});
        else ++sale.receipt[it->second].quantity;
    }
    sale.lines.reserve(sale.receipt.size());
    for (const auto& line : sale.receipt)
        sale.lines.push_back({catalog.at(line.index).sku, line.quantity, catalog.at(line.index).price_cents});
    std::sort(sale.lines.begin(), sale.lines.end(), [](const SaleLine& a, const SaleLine& b) { return a.sku < b.sku; });
    return sale;
}

void Transactions::reopen(Basket& basket) {
    std::lock_guard lock(basket.mu);
    basket.state = BasketState::Open;
}
