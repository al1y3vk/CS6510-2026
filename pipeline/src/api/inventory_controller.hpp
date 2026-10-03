#pragma once

#include <drogon/HttpController.h>

class InventoryController : public drogon::HttpController<InventoryController> {
public:
    METHOD_LIST_BEGIN
    ADD_METHOD_TO(InventoryController::lowStock, "/inventory/low-stock", drogon::Get);
    METHOD_LIST_END

    drogon::Task<drogon::HttpResponsePtr> lowStock(drogon::HttpRequestPtr req);
};
