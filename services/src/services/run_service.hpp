#pragma once

#include <functional>
#include <drogon/orm/DbClient.h>

#include "app_config.hpp"

// Usage: <service> [config.json] [port]   (defaults ./config.json, `defaultPort`)
// Every service shares config.json (DB + custom_config) and differs only in port. `init` runs once
// on the main loop after the DB client exists; it builds the service's state and publishes it.
// A throw from init logs and exits.
int runService(int argc, char** argv, uint16_t defaultPort,
               const std::function<void(const drogon::orm::DbClientPtr&, const AppConfig&)>& init);
