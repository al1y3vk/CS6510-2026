#include "controllers/analytics_controller.hpp"

#include <algorithm>
#include <chrono>

#include "money.hpp"
#include "query_int.hpp"
#include "services.hpp"

drogon::Task<drogon::HttpResponsePtr> AnalyticsController::popularItems(drogon::HttpRequestPtr req) {
    Services* s = gServices.load();
    if (!s) co_return notReady();

    bool bad = false;
    const long requested = queryInt(req, "limit", bad).value_or(10);
    if (bad) co_return apiError(drogon::k400BadRequest, "INVALID_REQUEST", "limit must be a non-negative integer");
    const auto limit = static_cast<Json::ArrayIndex>(std::clamp<long>(requested, 1, static_cast<long>(s->config.popularTopN)));

    std::optional<StoredSnapshot> snap;
    try {
        snap = co_await s->snapshots.latest();
    } catch (const drogon::orm::DrogonDbException& e) {
        LOG_ERROR << "snapshot read failed: " << e.base().what();
        co_return apiError(drogon::k500InternalServerError, "INTERNAL_ERROR", "could not read snapshot");
    }

    Json::Value body;
    body["windowSize"] = static_cast<Json::UInt64>(s->config.windowSize);
    body["slideInterval"] = static_cast<Json::UInt64>(s->config.slideInterval);
    Json::Value items(Json::arrayValue);
    if (snap) {
        body["windowStart"] = static_cast<Json::UInt64>(snap->windowStart);
        body["windowEnd"] = static_cast<Json::UInt64>(snap->windowEnd);
        body["computedAt"] = snap->computedAt;
        for (Json::ArrayIndex i = 0; i < snap->items.size() && i < limit; ++i) items.append(snap->items[i]);
    } else {
        body["windowStart"] = 0;
        body["windowEnd"] = 0;
        body["computedAt"] = rfc3339(std::chrono::system_clock::now());
    }
    body["items"] = std::move(items);
    co_return drogon::HttpResponse::newHttpJsonResponse(std::move(body));
}
