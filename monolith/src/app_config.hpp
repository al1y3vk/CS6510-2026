#pragma once

#include <cstddef>
#include <json/json.h>

struct AppConfig {
    int lowStockThreshold;
    size_t windowSize, slideInterval, popularTopN;
};

// Reads custom_config from config.json. Throws std::runtime_error on a missing or invalid key.
AppConfig loadAppConfig(const Json::Value& custom);
