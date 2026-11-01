#include "catalog_controller.hpp"

#include "services.hpp"

drogon::Task<drogon::HttpResponsePtr> CatalogController::items(drogon::HttpRequestPtr) {
    Services* s = gServices.load();
    if (!s) co_return notReady();

    co_return drogon::HttpResponse::newHttpJsonResponse(catalogJson(s->catalog));
}
