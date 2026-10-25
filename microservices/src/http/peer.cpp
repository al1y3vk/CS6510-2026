#include "http/peer.hpp"

#include <chrono>
#include <stdexcept>
#include <thread>
#include <trantor/utils/Logger.h>

namespace {
constexpr double kTimeoutSeconds = 10;
}  // namespace

Peer::Peer(const std::string& baseUrl) : loop_("peer") {
    loop_.run();
    client_ = drogon::HttpClient::newHttpClient(baseUrl, loop_.getLoop());
}

std::optional<Json::Value> Peer::get(const std::string& path) {
    auto req = drogon::HttpRequest::newHttpRequest();
    req->setPath(path);
    return send(req);
}

std::optional<Json::Value> Peer::post(const std::string& path, const Json::Value& body) {
    auto req = drogon::HttpRequest::newHttpJsonRequest(body);
    req->setMethod(drogon::Post);
    req->setPath(path);
    return send(req);
}

std::optional<Json::Value> Peer::send(const drogon::HttpRequestPtr& req) {
    auto [result, resp] = client_->sendRequest(req, kTimeoutSeconds);
    if (result != drogon::ReqResult::Ok || !resp) return std::nullopt;
    const int status = static_cast<int>(resp->statusCode());
    if (status < 200 || status > 299) return std::nullopt;
    auto json = resp->getJsonObject();
    return json ? *json : Json::Value{};
}

Catalog fetchCatalog(const std::string& catalogUrl) {
    Peer catalog(catalogUrl);
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(30);
    while (true) {
        if (auto body = catalog.get("/items")) return catalogFromJson(*body);
        if (std::chrono::steady_clock::now() > deadline)
            throw std::runtime_error("catalog service at " + catalogUrl + " did not answer GET /items");
        LOG_INFO << "waiting for catalog service at " << catalogUrl;
        std::this_thread::sleep_for(std::chrono::milliseconds(200));
    }
}
