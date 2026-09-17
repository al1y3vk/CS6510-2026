#include "controllers/catalog_controller.hpp"

#include "money.hpp"
#include "services.hpp"

drogon::Task<drogon::HttpResponsePtr> CatalogController::items(drogon::HttpRequestPtr) {
    Services* s = gServices.load();
    if (!s) co_return notReady();

    Json::Value items(Json::arrayValue);
    for (const Item& item : s->catalog.all()) {
        Json::Value entry;
        entry["sku"] = item.sku;
        entry["name"] = item.name;
        entry["price"] = centsToDouble(item.price_cents);
        items.append(std::move(entry));
    }
    Json::Value body;
    body["items"] = std::move(items);
    co_return drogon::HttpResponse::newHttpJsonResponse(std::move(body));
}
