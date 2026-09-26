#include <catch2/catch_test_macros.hpp>
#include <chrono>

#include "money.hpp"

TEST_CASE("centsToDouble converts whole cents") {
    REQUIRE(centsToDouble(185) == 1.85);
    REQUIRE(centsToDouble(0) == 0.0);
}

TEST_CASE("pgArray formats SKUs as a Postgres array literal") {
    REQUIRE(pgArray(std::vector<std::string>{"SKU-000001", "SKU-000002"}) == "{SKU-000001,SKU-000002}");
    REQUIRE(pgArray(std::vector<std::string>{}) == "{}");
}

TEST_CASE("pgArray formats ints") {
    REQUIRE(pgArray(std::vector<int64_t>{2, 1, 30}) == "{2,1,30}");
}

TEST_CASE("rfc3339 is UTC with millisecond precision") {
    using namespace std::chrono;
    system_clock::time_point tp{milliseconds{1'700'000'000'123}};
    REQUIRE(rfc3339(tp) == "2023-11-14T22:13:20.123Z");
}
