#pragma once

#include <drogon/HttpController.h>

class AnalyticsController : public drogon::HttpController<AnalyticsController> {
public:
    METHOD_LIST_BEGIN
    ADD_METHOD_TO(AnalyticsController::popularItems, "/analytics/popular-items", drogon::Get);
    METHOD_LIST_END

    drogon::Task<drogon::HttpResponsePtr> popularItems(drogon::HttpRequestPtr req);
};
