#include "transactions.hpp"

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
