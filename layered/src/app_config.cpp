#include "app_config.hpp"

#include <stdexcept>
#include <string>

namespace {
int64_t positiveInt(const Json::Value& custom, const char* key) {
    const Json::Value& v = custom[key];
    if (!v.isIntegral() || v.asInt64() <= 0)
        throw std::runtime_error(std::string("custom_config.") + key + " must be a positive integer");
    return v.asInt64();
}
}  // namespace

AppConfig loadAppConfig(const Json::Value& custom) {
    return AppConfig{
        .lowStockThreshold = static_cast<int>(positiveInt(custom, "low_stock_threshold")),
        .windowSize = static_cast<size_t>(positiveInt(custom, "window_size")),
        .slideInterval = static_cast<size_t>(positiveInt(custom, "slide_interval")),
        .popularTopN = static_cast<size_t>(positiveInt(custom, "popular_top_n")),
    };
}
