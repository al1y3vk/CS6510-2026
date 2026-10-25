#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <vector>
#include <json/json.h>

// "Sale completed" event: what checkout's outbox relay sends and inventory applies.
struct SaleEvent {
    struct Line {
        std::string sku;
        int32_t quantity;
    };
    std::string transactionId;
    std::vector<Line> lines;
};

// {"sales": [{"transactionId": "...", "lines": [{"sku": "...", "quantity": 2}, ...]}, ...]}
Json::Value saleEventsJson(const std::vector<SaleEvent>& sales);
// nullopt if the body is not that shape (missing fields, non-positive quantity, no lines).
std::optional<std::vector<SaleEvent>> parseSaleEvents(const Json::Value& body);
