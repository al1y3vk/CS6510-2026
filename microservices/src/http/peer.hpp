#pragma once

#include <optional>
#include <string>
#include <drogon/HttpClient.h>
#include <json/json.h>
#include <trantor/net/EventLoopThread.h>

#include "catalog.hpp"

// Blocking JSON-over-HTTP calls to one other service. Owns its own event loop thread, so the
// calls block only the caller: use it from a worker thread or startup, never a Drogon IO thread.
class Peer {
public:
    explicit Peer(const std::string& baseUrl);
    // The response body on a 2xx; nullopt on a connection error, timeout, or any other status.
    std::optional<Json::Value> get(const std::string& path);
    std::optional<Json::Value> post(const std::string& path, const Json::Value& body);
private:
    std::optional<Json::Value> send(const drogon::HttpRequestPtr& req);
    trantor::EventLoopThread loop_;
    drogon::HttpClientPtr client_;
};

// GET <catalogUrl>/items, retried every 200 ms until the catalog service answers (gives up and
// throws after 30 s). How every service except catalog gets sku/name/price.
Catalog fetchCatalog(const std::string& catalogUrl);
