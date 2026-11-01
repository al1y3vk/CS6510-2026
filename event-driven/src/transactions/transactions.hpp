#pragma once

#include <chrono>
#include <cstdint>
#include <memory>
#include <mutex>
#include <optional>
#include <shared_mutex>
#include <string>
#include <unordered_map>
#include <vector>

#include "catalog.hpp"

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

// One line of a sale, as complete_sale() and the receipt both need it.
struct SaleLine { std::string sku; int32_t quantity; int64_t price_cents; };

struct ScanResult { int32_t itemCount; int64_t totalPriceCents; };

enum class CompleteStatus { Ok, NotOpen, EmptyBasket };

struct ReceiptLine { uint32_t index; int32_t quantity; };

struct PreparedSale {
    CompleteStatus status = CompleteStatus::Ok;
    std::vector<ReceiptLine> receipt;   // first-scan order, for the response
    std::vector<SaleLine> lines;        // same lines sorted by sku, for complete_sale()
    int32_t itemCount = 0;
    int64_t totalPriceCents = 0;
};

class Transactions {
public:
    std::shared_ptr<Basket> start(std::string stationId);
    std::shared_ptr<Basket> find(const std::string& basketId) const;
    void remove(const std::string& basketId);
    size_t openCount() const;

    // Pure basket logic below: no DB, no HTTP. The caller maps the result to a response.

    // nullopt if the basket is no longer Open.
    std::optional<ScanResult> scan(Basket& basket, const Item& item);
    // On Ok, also flips the basket to Completing and groups/sorts the scanned items into lines.
    PreparedSale prepareComplete(Basket& basket, const Catalog& catalog);
    // Rolls a Completing basket back to Open after a failed DB commit.
    void reopen(Basket& basket);
private:
    mutable std::shared_mutex mu_;
    std::unordered_map<std::string, std::shared_ptr<Basket>> open_;
};
