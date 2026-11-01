#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>
#include <json/json.h>

// The events the services exchange, one Kafka topic each. This file is the whole contract between
// services: no service calls another one directly. Values are compact JSON. Events with the same
// key go to the same partition, so Kafka keeps them in order.
namespace topic {
// Published by catalog at startup. Key "items"; value = the whole catalog, same shape as the
// GET /items body. The topic is compacted, so the newest catalog stays readable from the start
// of the topic no matter how late a service starts.
inline constexpr const char* kCatalog = "catalog.items";
// Published by checkout's outbox relay, consumed by inventory. Key = transaction id.
inline constexpr const char* kSales = "sales.completed";
// Published by checkout per scanned item, consumed by analytics. Key = transaction id.
inline constexpr const char* kScans = "items.scanned";
}  // namespace topic

// sales.completed: {"transactionId": "...", "lines": [{"sku": "...", "quantity": 2}, ...]}
struct SaleEvent {
    struct Line {
        std::string sku;
        int32_t quantity;
    };
    std::string transactionId;
    std::vector<Line> lines;
};
std::string saleEventJson(const SaleEvent& sale);
// nullopt if the value is not that shape (bad JSON, missing fields, non-positive quantity, no lines).
std::optional<SaleEvent> parseSaleEvent(std::string_view value);

// items.scanned: {"transactionId": "...", "sku": "..."}
struct ScanEvent {
    std::string transactionId;
    std::string sku;
};
std::string scanEventJson(const ScanEvent& scan);
std::optional<ScanEvent> parseScanEvent(std::string_view value);

// Compact JSON text, and back (nullopt on a parse error).
std::string toJson(const Json::Value& value);
std::optional<Json::Value> parseJson(std::string_view text);
