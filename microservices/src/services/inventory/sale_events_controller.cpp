#include "sale_events_controller.hpp"

#include "services.hpp"

drogon::Task<drogon::HttpResponsePtr> SaleEventsController::sales(drogon::HttpRequestPtr req) {
    Services* s = gServices.load();
    if (!s) co_return notReady();

    const auto& json = req->getJsonObject();
    auto sales = json ? parseSaleEvents(*json) : std::nullopt;
    if (!sales) co_return apiError(drogon::k400BadRequest, "INVALID_REQUEST", "expected {\"sales\": [...]}");

    int applied = 0;
    try {
        applied = co_await s->stock.applySales(*sales);
    } catch (const drogon::orm::DrogonDbException& e) {
        LOG_ERROR << "apply_sales (" << sales->size() << " sales) failed: " << e.base().what();
        co_return apiError(drogon::k500InternalServerError, "INTERNAL_ERROR", "could not apply sales");
    }
    Json::Value body;
    body["received"] = static_cast<Json::UInt64>(sales->size());
    body["applied"] = applied;   // < received when the relay resent a batch
    co_return drogon::HttpResponse::newHttpJsonResponse(std::move(body));
}
