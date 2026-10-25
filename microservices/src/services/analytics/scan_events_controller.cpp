#include "scan_events_controller.hpp"

#include "services.hpp"

drogon::Task<drogon::HttpResponsePtr> ScanEventsController::scans(drogon::HttpRequestPtr req) {
    Services* s = gServices.load();
    if (!s) co_return notReady();

    // {"items": [itemIndex, ...]}; indexes are catalog order, which both services got from /items.
    const auto& json = req->getJsonObject();
    if (!json || !(*json)["items"].isArray())
        co_return apiError(drogon::k400BadRequest, "INVALID_REQUEST", "expected {\"items\": [...]}");
    const Json::Value& items = (*json)["items"];
    for (const Json::Value& i : items)
        if (!i.isUInt() || i.asUInt() >= s->catalog.size())
            co_return apiError(drogon::k400BadRequest, "INVALID_REQUEST", "item index out of range");

    for (const Json::Value& i : items) s->analytics.recordScan(i.asUInt());
    Json::Value body;
    body["accepted"] = items.size();
    co_return drogon::HttpResponse::newHttpJsonResponse(std::move(body));
}
