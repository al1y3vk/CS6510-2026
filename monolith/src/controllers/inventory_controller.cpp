#include "controllers/inventory_controller.hpp"

#include <chrono>

#include "money.hpp"
#include "query_int.hpp"
#include "services.hpp"

drogon::Task<drogon::HttpResponsePtr> InventoryController::lowStock(drogon::HttpRequestPtr req) {
    Services* s = gServices.load();
    if (!s) co_return notReady();

    bool bad = false;
    const int threshold = static_cast<int>(queryInt(req, "threshold", bad).value_or(s->config.lowStockThreshold));
    if (bad) co_return apiError(drogon::k400BadRequest, "INVALID_REQUEST", "threshold must be a non-negative integer");

    std::vector<LowStockRow> rows;
    try {
        rows = co_await s->inventory.lowStock(threshold);
    } catch (const drogon::orm::DrogonDbException& e) {
        LOG_ERROR << "low stock query failed: " << e.base().what();
        co_return apiError(drogon::k500InternalServerError, "INTERNAL_ERROR", "could not read stock");
    }
    const std::string generatedAt = rfc3339(std::chrono::system_clock::now());

    Json::Value alerts(Json::arrayValue);
    for (const LowStockRow& row : rows) {
        Json::Value alert;
        alert["sku"] = row.sku;
        alert["name"] = row.name;
        alert["currentStock"] = row.quantity;
        alert["threshold"] = threshold;
        alert["triggeredAt"] = row.since.value_or(generatedAt);   // spec §15.4: override rows may have no since
        alerts.append(std::move(alert));
    }
    Json::Value body;
    body["threshold"] = threshold;
    body["generatedAt"] = generatedAt;
    body["alerts"] = std::move(alerts);
    co_return drogon::HttpResponse::newHttpJsonResponse(std::move(body));
}
