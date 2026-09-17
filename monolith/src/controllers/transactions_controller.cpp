#include "controllers/transactions_controller.hpp"

#include <algorithm>
#include <mutex>
#include <unordered_map>

#include "money.hpp"
#include "services.hpp"

using drogon::HttpResponse;
using drogon::HttpResponsePtr;
using drogon::Task;

namespace {
// Body must be JSON with a non-empty string at `key`; returns it, or empty on any failure.
std::string requiredString(const drogon::HttpRequestPtr& req, const char* key) {
    const auto& json = req->getJsonObject();
    if (!json || !(*json)[key].isString()) return {};
    return (*json)[key].asString();
}

Json::Value transactionJson(const std::string& id, const std::string& stationId, const char* status,
                            int32_t itemCount, int64_t totalCents, const std::string& startedAt) {
    Json::Value body;
    body["transactionId"] = id;
    body["stationId"] = stationId;
    body["status"] = status;
    body["itemCount"] = itemCount;
    body["runningTotal"] = centsToDouble(totalCents);
    body["startedAt"] = startedAt;
    return body;
}

// A missing basket is 409 if the id was already completed, else 404.
Task<HttpResponsePtr> notOpen(Services& s, std::string txId) {
    if (co_await s.inventory.findSale(txId))
        co_return apiError(drogon::k409Conflict, "TRANSACTION_NOT_OPEN", "transaction already completed");
    co_return apiError(drogon::k404NotFound, "NOT_FOUND", "unknown transaction");
}
}  // namespace

Task<HttpResponsePtr> TransactionsController::start(drogon::HttpRequestPtr req) {
    Services* s = gServices.load();
    if (!s) co_return notReady();

    const std::string stationId = requiredString(req, "stationId");
    if (stationId.empty()) co_return apiError(drogon::k400BadRequest, "INVALID_REQUEST", "stationId is required");

    auto basket = s->transactions.start(stationId);
    auto resp = HttpResponse::newHttpJsonResponse(
        transactionJson(basket->id, basket->stationId, "OPEN", 0, 0, rfc3339(basket->startedAt)));
    resp->setStatusCode(drogon::k201Created);
    co_return resp;
}

Task<HttpResponsePtr> TransactionsController::scan(drogon::HttpRequestPtr req, std::string txId) {
    Services* s = gServices.load();
    if (!s) co_return notReady();

    const std::string sku = requiredString(req, "sku");
    if (sku.empty()) co_return apiError(drogon::k400BadRequest, "INVALID_REQUEST", "sku is required");

    auto basket = s->transactions.find(txId);
    if (!basket) co_return co_await notOpen(*s, txId);

    const Item* item = s->catalog.find(sku);
    if (!item) co_return apiError(drogon::k404NotFound, "UNKNOWN_SKU", "unknown sku " + sku);

    int32_t itemCount;
    int64_t totalCents;
    {
        std::lock_guard lock(basket->mu);
        if (basket->state != BasketState::Open)
            co_return apiError(drogon::k409Conflict, "TRANSACTION_NOT_OPEN", "transaction is completing");
        basket->itemIndices.push_back(item->index);
        basket->totalPriceCents += item->price_cents;
        itemCount = static_cast<int32_t>(basket->itemIndices.size());
        totalCents = basket->totalPriceCents;
    }

    if (auto snap = s->window.record(item->index)) s->snapshots.insertAsync(*snap, s->catalog);

    Json::Value body;
    body["transactionId"] = txId;
    body["sku"] = item->sku;
    body["name"] = item->name;
    body["unitPrice"] = centsToDouble(item->price_cents);
    body["itemCount"] = itemCount;
    body["runningTotal"] = centsToDouble(totalCents);
    co_return HttpResponse::newHttpJsonResponse(std::move(body));
}

Task<HttpResponsePtr> TransactionsController::complete(drogon::HttpRequestPtr, std::string txId) {
    Services* s = gServices.load();
    if (!s) co_return notReady();

    auto basket = s->transactions.find(txId);
    if (!basket) co_return co_await notOpen(*s, txId);

    std::vector<uint32_t> indices;
    int64_t totalCents;
    {
        std::lock_guard lock(basket->mu);
        if (basket->state != BasketState::Open)
            co_return apiError(drogon::k409Conflict, "TRANSACTION_NOT_OPEN", "transaction is completing");
        if (basket->itemIndices.empty())
            co_return apiError(drogon::k409Conflict, "EMPTY_BASKET", "no items scanned");
        basket->state = BasketState::Completing;
        indices = basket->itemIndices;
        totalCents = basket->totalPriceCents;
    }

    // Receipt lines in first-scan order; the same lines sorted by SKU for complete_sale().
    std::vector<std::pair<uint32_t, int32_t>> receipt;   // (index, quantity)
    std::unordered_map<uint32_t, size_t> lineOf;
    for (uint32_t index : indices) {
        auto [it, inserted] = lineOf.try_emplace(index, receipt.size());
        if (inserted) receipt.emplace_back(index, 1);
        else ++receipt[it->second].second;
    }
    std::vector<SaleLine> lines;
    for (auto [index, qty] : receipt) lines.push_back({s->catalog.at(index).sku, qty, s->catalog.at(index).price_cents});
    std::sort(lines.begin(), lines.end(), [](const SaleLine& a, const SaleLine& b) { return a.sku < b.sku; });

    const auto itemCount = static_cast<int32_t>(indices.size());
    std::string completedAt;
    try {
        completedAt = co_await s->inventory.completeSale(*basket, lines, itemCount, totalCents);
    } catch (const drogon::orm::DrogonDbException& e) {
        {
            std::lock_guard lock(basket->mu);
            basket->state = BasketState::Open;
        }
        LOG_ERROR << "complete_sale failed for " << txId << ": " << e.base().what();
        co_return apiError(drogon::k500InternalServerError, "INTERNAL_ERROR", "could not record the sale");
    }
    s->transactions.remove(txId);

    Json::Value jsonLines(Json::arrayValue);
    for (auto [index, qty] : receipt) {
        const Item& item = s->catalog.at(index);
        Json::Value line;
        line["sku"] = item.sku;
        line["name"] = item.name;
        line["unitPrice"] = centsToDouble(item.price_cents);
        line["quantity"] = qty;
        jsonLines.append(std::move(line));
    }
    Json::Value body;
    body["transactionId"] = txId;
    body["stationId"] = basket->stationId;
    body["itemCount"] = itemCount;
    body["totalAmount"] = centsToDouble(totalCents);
    body["startedAt"] = rfc3339(basket->startedAt);
    body["completedAt"] = completedAt;
    body["lines"] = std::move(jsonLines);
    co_return HttpResponse::newHttpJsonResponse(std::move(body));
}

Task<HttpResponsePtr> TransactionsController::status(drogon::HttpRequestPtr, std::string txId) {
    Services* s = gServices.load();
    if (!s) co_return notReady();

    if (auto basket = s->transactions.find(txId)) {
        std::lock_guard lock(basket->mu);   // a Completing basket still reports OPEN until the commit lands
        co_return HttpResponse::newHttpJsonResponse(transactionJson(
            basket->id, basket->stationId, "OPEN", static_cast<int32_t>(basket->itemIndices.size()),
            basket->totalPriceCents, rfc3339(basket->startedAt)));
    }
    if (auto sale = co_await s->inventory.findSale(txId)) {
        co_return HttpResponse::newHttpJsonResponse(transactionJson(
            txId, sale->stationId, "COMPLETED", sale->itemCount, sale->totalPriceCents, sale->startedAt));
    }
    co_return apiError(drogon::k404NotFound, "NOT_FOUND", "unknown transaction");
}
