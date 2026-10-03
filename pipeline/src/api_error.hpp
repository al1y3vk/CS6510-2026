#pragma once

#include <drogon/HttpResponse.h>
#include <string>
#include <string_view>

// {"error": code, "message": message} with the given status. Codes match the mock server (spec §7.3).
inline drogon::HttpResponsePtr apiError(drogon::HttpStatusCode status, std::string_view code, std::string_view message) {
    Json::Value body;
    body["error"] = std::string(code);
    body["message"] = std::string(message);
    auto resp = drogon::HttpResponse::newHttpJsonResponse(std::move(body));
    resp->setStatusCode(status);
    return resp;
}
