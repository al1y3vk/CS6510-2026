#include "sale_event.hpp"

Json::Value saleEventsJson(const std::vector<SaleEvent>& sales) {
    Json::Value jsonSales(Json::arrayValue);
    for (const SaleEvent& sale : sales) {
        Json::Value lines(Json::arrayValue);
        for (const SaleEvent::Line& l : sale.lines) {
            Json::Value line;
            line["sku"] = l.sku;
            line["quantity"] = l.quantity;
            lines.append(std::move(line));
        }
        Json::Value jsonSale;
        jsonSale["transactionId"] = sale.transactionId;
        jsonSale["lines"] = std::move(lines);
        jsonSales.append(std::move(jsonSale));
    }
    Json::Value body;
    body["sales"] = std::move(jsonSales);
    return body;
}

std::optional<std::vector<SaleEvent>> parseSaleEvents(const Json::Value& body) {
    if (!body.isObject() || !body["sales"].isArray()) return std::nullopt;
    std::vector<SaleEvent> sales;
    sales.reserve(body["sales"].size());
    for (const Json::Value& s : body["sales"]) {
        if (!s["transactionId"].isString() || !s["lines"].isArray() || s["lines"].empty()) return std::nullopt;
        SaleEvent sale{.transactionId = s["transactionId"].asString(), .lines = {}};
        for (const Json::Value& l : s["lines"]) {
            if (!l["sku"].isString() || !l["quantity"].isInt() || l["quantity"].asInt() <= 0) return std::nullopt;
            sale.lines.push_back({l["sku"].asString(), l["quantity"].asInt()});
        }
        sales.push_back(std::move(sale));
    }
    return sales;
}
