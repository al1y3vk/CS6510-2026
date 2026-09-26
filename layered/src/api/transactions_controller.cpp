#include "api/transactions_controller.hpp"

#include <mutex>

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

    auto result = s->transactions.scan(*basket, *item);
    if (!result) co_return apiError(drogon::k409Conflict, "TRANSACTION_NOT_OPEN", "transaction is completing");

    s->analytics.recordScan(item->index, s->snapshots, s->catalog);

    Json::Value body;
    body["transactionId"] = txId;
    body["sku"] = item->sku;
    body["name"] = item->name;
    body["unitPrice"] = centsToDouble(item->price_cents);
    body["itemCount"] = result->itemCount;
    body["runningTotal"] = centsToDouble(result->totalPriceCents);
    co_return HttpResponse::newHttpJsonResponse(std::move(body));
}

Task<HttpResponsePtr> TransactionsController::complete(drogon::HttpRequestPtr, std::string txId) {
    Services* s = gServices.load();
    if (!s) co_return notReady();

    auto basket = s->transactions.find(txId);
    if (!basket) co_return co_await notOpen(*s, txId);

    auto sale = s->transactions.prepareComplete(*basket, s->catalog);
    if (sale.status == CompleteStatus::NotOpen)
        co_return apiError(drogon::k409Conflict, "TRANSACTION_NOT_OPEN", "transaction is completing");
    if (sale.status == CompleteStatus::EmptyBasket)
        co_return apiError(drogon::k409Conflict, "EMPTY_BASKET", "no items scanned");

    std::string completedAt;
    try {
        completedAt = co_await s->inventory.completeSale(*basket, sale.lines, sale.itemCount, sale.totalPriceCents);
    } catch (const drogon::orm::DrogonDbException& e) {
        s->transactions.reopen(*basket);
        LOG_ERROR << "complete_sale failed for " << txId << ": " << e.base().what();
        co_return apiError(drogon::k500InternalServerError, "INTERNAL_ERROR", "could not record the sale");
    }
    s->transactions.remove(txId);

    Json::Value jsonLines(Json::arrayValue);
    for (const auto& line : sale.receipt) {
        const Item& item = s->catalog.at(line.index);
        Json::Value jsonLine;
        jsonLine["sku"] = item.sku;
        jsonLine["name"] = item.name;
        jsonLine["unitPrice"] = centsToDouble(item.price_cents);
        jsonLine["quantity"] = line.quantity;
        jsonLines.append(std::move(jsonLine));
    }
    Json::Value body;
    body["transactionId"] = txId;
    body["stationId"] = basket->stationId;
    body["itemCount"] = sale.itemCount;
    body["totalAmount"] = centsToDouble(sale.totalPriceCents);
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
