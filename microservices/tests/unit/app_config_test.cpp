#include <catch2/catch_test_macros.hpp>
#include <stdexcept>

#include "app_config.hpp"

namespace {
Json::Value full() {
    Json::Value v;
    v["low_stock_threshold"] = 50;
    v["window_size"] = 1000;
    v["slide_interval"] = 500;
    v["popular_top_n"] = 10;
    return v;
}
}  // namespace

TEST_CASE("loadAppConfig reads every key") {
    AppConfig c = loadAppConfig(full());
    REQUIRE(c.lowStockThreshold == 50);
    REQUIRE(c.windowSize == 1000);
    REQUIRE(c.slideInterval == 500);
    REQUIRE(c.popularTopN == 10);
}

TEST_CASE("loadAppConfig throws on a missing key") {
    auto v = full();
    v.removeMember("slide_interval");
    REQUIRE_THROWS_AS(loadAppConfig(v), std::runtime_error);
}

TEST_CASE("loadAppConfig throws on a non-positive value") {
    auto v = full();
    v["window_size"] = 0;
    REQUIRE_THROWS_AS(loadAppConfig(v), std::runtime_error);
}
