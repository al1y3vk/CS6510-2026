#pragma once

#include <functional>
#include <string>
#include <drogon/orm/DbClient.h>

#include "app_config.hpp"

// Usage: <service> [config/<service>.json] [port]   (defaults ./config.json, `defaultPort`)
// Each service has its own config file: its own database plus the URLs of the services it calls.
// `init` runs once on the main loop after the DB client exists; it builds the service's state and
// publishes it.
// A throw from init logs and exits.
int runService(int argc, char** argv, uint16_t defaultPort,
               const std::function<void(const drogon::orm::DbClientPtr&, const AppConfig&)>& init);

// custom_config.<key> as a string (e.g. "catalog_url"). Throws std::runtime_error if missing.
std::string customString(const char* key);
