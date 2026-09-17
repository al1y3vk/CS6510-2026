#pragma once

#include <chrono>
#include <cstdint>
#include <memory>
#include <mutex>
#include <shared_mutex>
#include <string>
#include <unordered_map>
#include <vector>

enum class BasketState { Open, Completing };

struct Basket {
    std::string id;
    std::string stationId;
    std::chrono::system_clock::time_point startedAt;
    std::mutex mu; // this guards the fields below
    BasketState state = BasketState::Open;
    std::vector<uint32_t> itemIndices;
    int64_t totalPriceCents = 0;
};

class Transactions {
public:
    std::shared_ptr<Basket> start(std::string stationId);
    std::shared_ptr<Basket> find(const std::string& basketId) const;
    void remove(const std::string& basketId);
    size_t openCount() const;
private:
    mutable std::shared_mutex mu_;
    std::unordered_map<std::string, std::shared_ptr<Basket>> open_;
};