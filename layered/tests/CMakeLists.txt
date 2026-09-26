find_package(Catch2 3 REQUIRED)
include(Catch)                                  # catch_discover_tests: one ctest entry per TEST_CASE

add_executable(checkout-unit-tests
    unit/money_test.cpp unit/catalog_test.cpp unit/app_config_test.cpp
    unit/transactions_test.cpp unit/popularity_window_test.cpp)
target_link_libraries(checkout-unit-tests PRIVATE checkout_core Catch2::Catch2WithMain)
catch_discover_tests(checkout-unit-tests PROPERTIES LABELS unit)

add_executable(checkout-db-tests db/inventory_test.cpp db/snapshots_test.cpp)
target_link_libraries(checkout-db-tests PRIVATE checkout_db Catch2::Catch2WithMain)
target_compile_definitions(checkout-db-tests PRIVATE MONOLITH_DIR="${CMAKE_SOURCE_DIR}")   # fixture runs db/reset.sh
catch_discover_tests(checkout-db-tests PROPERTIES LABELS db)
