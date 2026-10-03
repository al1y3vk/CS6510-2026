#include "api/analytics_controller.hpp"

#include <algorithm>

#include "query_int.hpp"
#include "services.hpp"

drogon::Task<drogon::HttpResponsePtr> AnalyticsController::popularItems(drogon::HttpRequestPtr req) {
    Services* s = gServices.load();
    if (!s) co_return notReady();

    bool bad = false;
    const long requested = queryInt(req, "limit", bad).value_or(10);
    if (bad) co_return apiError(drogon::k400BadRequest, "INVALID_REQUEST", "limit must be a non-negative integer");
    const int limit = static_cast<int>(std::clamp<long>(requested, 1, static_cast<long>(s->config.popularTopN)));

    Json::Value body;
    try {
        body = co_await s->analytics.popularItemsJson(limit, s->snapshots);
    } catch (const drogon::orm::DrogonDbException& e) {
        LOG_ERROR << "snapshot read failed: " << e.base().what();
        co_return apiError(drogon::k500InternalServerError, "INTERNAL_ERROR", "could not read snapshot");
    }
    co_return drogon::HttpResponse::newHttpJsonResponse(std::move(body));
}
