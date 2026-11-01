#include "events.hpp"

#include <memory>

std::string toJson(const Json::Value& value) {
    static const Json::StreamWriterBuilder compact = [] {
        Json::StreamWriterBuilder b;
        b["indentation"] = "";
        return b;
    }();
    return Json::writeString(compact, value);
}

std::optional<Json::Value> parseJson(std::string_view text) {
    thread_local std::unique_ptr<Json::CharReader> reader{Json::CharReaderBuilder{}.newCharReader()};
    Json::Value value;
    if (!reader->parse(text.data(), text.data() + text.size(), &value, nullptr)) return std::nullopt;
    return value;
}

std::string saleEventJson(const SaleEvent& sale) {
    Json::Value lines(Json::arrayValue);
    for (const SaleEvent::Line& l : sale.lines) {
        Json::Value line;
        line["sku"] = l.sku;
        line["quantity"] = l.quantity;
        lines.append(std::move(line));
    }
    Json::Value event;
    event["transactionId"] = sale.transactionId;
    event["lines"] = std::move(lines);
    return toJson(event);
}

std::optional<SaleEvent> parseSaleEvent(std::string_view value) {
    auto s = parseJson(value);
    if (!s || !s->isObject() || !(*s)["transactionId"].isString() || !(*s)["lines"].isArray() ||
        (*s)["lines"].empty())
        return std::nullopt;
    SaleEvent sale{.transactionId = (*s)["transactionId"].asString(), .lines = {}};
    for (const Json::Value& l : (*s)["lines"]) {
        if (!l.isObject() || !l["sku"].isString() || !l["quantity"].isInt() || l["quantity"].asInt() <= 0)
            return std::nullopt;
        sale.lines.push_back({l["sku"].asString(), l["quantity"].asInt()});
    }
    return sale;
}

std::string scanEventJson(const ScanEvent& scan) {
    Json::Value event;
    event["transactionId"] = scan.transactionId;
    event["sku"] = scan.sku;
    return toJson(event);
}

std::optional<ScanEvent> parseScanEvent(std::string_view value) {
    auto s = parseJson(value);
    if (!s || !s->isObject() || !(*s)["transactionId"].isString() || !(*s)["sku"].isString()) return std::nullopt;
    return ScanEvent{(*s)["transactionId"].asString(), (*s)["sku"].asString()};
}
