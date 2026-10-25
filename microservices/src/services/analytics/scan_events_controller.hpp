#pragma once

#include <drogon/HttpController.h>

// Internal: checkout's publisher posts batches of scanned item indexes here. Not routed by the gateway.
class ScanEventsController : public drogon::HttpController<ScanEventsController> {
public:
    METHOD_LIST_BEGIN
    ADD_METHOD_TO(ScanEventsController::scans, "/events/scans", drogon::Post);
    METHOD_LIST_END

    drogon::Task<drogon::HttpResponsePtr> scans(drogon::HttpRequestPtr req);
};
