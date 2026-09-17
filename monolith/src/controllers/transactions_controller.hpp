#pragma once

#include <drogon/HttpController.h>
#include <string>

class TransactionsController : public drogon::HttpController<TransactionsController> {
public:
    METHOD_LIST_BEGIN
    ADD_METHOD_TO(TransactionsController::start,    "/transactions",              drogon::Post);
    ADD_METHOD_TO(TransactionsController::scan,     "/transactions/{1}/items",    drogon::Post);
    ADD_METHOD_TO(TransactionsController::complete, "/transactions/{1}/complete", drogon::Post);
    ADD_METHOD_TO(TransactionsController::status,   "/transactions/{1}",          drogon::Get);
    METHOD_LIST_END

    drogon::Task<drogon::HttpResponsePtr> start(drogon::HttpRequestPtr req);
    drogon::Task<drogon::HttpResponsePtr> scan(drogon::HttpRequestPtr req, std::string txId);
    drogon::Task<drogon::HttpResponsePtr> complete(drogon::HttpRequestPtr req, std::string txId);
    drogon::Task<drogon::HttpResponsePtr> status(drogon::HttpRequestPtr req, std::string txId);
};
