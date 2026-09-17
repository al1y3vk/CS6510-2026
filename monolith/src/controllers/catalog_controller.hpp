#pragma once

#include <drogon/HttpController.h>

class CatalogController : public drogon::HttpController<CatalogController> {
public:
    METHOD_LIST_BEGIN
    ADD_METHOD_TO(CatalogController::items, "/items", drogon::Get);
    METHOD_LIST_END

    drogon::Task<drogon::HttpResponsePtr> items(drogon::HttpRequestPtr req);
};
