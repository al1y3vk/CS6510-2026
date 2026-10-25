#pragma once

#include <drogon/HttpController.h>

// Internal: checkout's outbox relay posts completed sales here. Not routed by the gateway.
class SaleEventsController : public drogon::HttpController<SaleEventsController> {
public:
    METHOD_LIST_BEGIN
    ADD_METHOD_TO(SaleEventsController::sales, "/events/sales", drogon::Post);
    METHOD_LIST_END

    drogon::Task<drogon::HttpResponsePtr> sales(drogon::HttpRequestPtr req);
};
