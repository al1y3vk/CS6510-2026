#include "services/run_service.hpp"

#include <cstdlib>
#include <stdexcept>
#include <string>
#include <drogon/drogon.h>

int runService(int argc, char** argv, uint16_t defaultPort,
               const std::function<void(const drogon::orm::DbClientPtr&, const AppConfig&)>& init) {
    auto& app = drogon::app();
    app.loadConfigFile(argc > 1 ? argv[1] : "config.json");
    const auto port = argc > 2 ? static_cast<uint16_t>(std::stoi(argv[2])) : defaultPort;
    app.addListener("0.0.0.0", port);

    app.registerBeginningAdvice([init, port] {
        // The non-fast client has its own threads, so blocking here with execSqlSync is fine.
        try {
            init(drogon::app().getDbClient("default"), loadAppConfig(drogon::app().getCustomConfig()));
            LOG_INFO << "ready on :" << port;
        } catch (const std::exception& e) {
            LOG_ERROR << "startup failed: " << e.what();
            std::exit(1);
        }
    });
    app.run();
    return 0;
}

std::string customString(const char* key) {
    const Json::Value& v = drogon::app().getCustomConfig()[key];
    if (!v.isString()) throw std::runtime_error(std::string("custom_config.") + key + " must be a string");
    return v.asString();
}
