cmake_minimum_required(VERSION 3.28)
project(checkout LANGUAGES CXX)
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)         # clangd reads build/<preset>/compile_commands.json

# Ubuntu's libdrogon-dev ships stale Find modules: FindJsoncpp uses exec_program (an error
# under 3.28 policies) and FindMySQL looks for the pre-MySQL-8 name mysqlclient_r.
cmake_policy(SET CMP0153 OLD)
find_library(MYSQL_LIBRARIES NAMES mysqlclient mysqlclient_r mariadbclient)
find_package(Drogon CONFIG REQUIRED)
find_package(PkgConfig REQUIRED)
pkg_check_modules(RDKAFKA REQUIRED IMPORTED_TARGET rdkafka++)   # apt: librdkafka-dev

add_library(checkout_core STATIC
    src/money.cpp src/catalog.cpp src/events.cpp src/transactions/transactions.cpp
    src/analytics/filters.cpp src/analytics/pipeline.cpp src/app_config.cpp)
target_include_directories(checkout_core PUBLIC src)
target_link_libraries(checkout_core PUBLIC Jsoncpp_lib)     # app_config, catalog, events read/write Json::Value

# DB access: one library per service database. No service links another service's library.
add_library(db_catalog STATIC src/db/items.cpp)                         # catalog db: items
add_library(db_checkout STATIC src/db/sales.cpp src/db/sale_outbox.cpp) # checkout db: sales, receipt_lines, sale_outbox
add_library(db_inventory STATIC src/db/stock.cpp)                       # inventory db: stock, applied_sales
add_library(db_analytics STATIC src/db/snapshots.cpp)                   # analytics db: popular_snapshots
foreach(lib db_catalog db_checkout db_inventory db_analytics)
    target_link_libraries(${lib} PUBLIC checkout_core Drogon::Drogon)
endforeach()

# Kafka producer/consumer: the only way the services talk to each other.
add_library(bus STATIC src/bus/bus.cpp)
target_link_libraries(bus PUBLIC checkout_core Drogon::Drogon PkgConfig::RDKAFKA)

add_library(service_main STATIC src/services/run_service.cpp)   # config + port + startup, same for all four
target_link_libraries(service_main PUBLIC checkout_core Drogon::Drogon)

add_executable(catalog-service src/services/catalog/main.cpp src/services/catalog/catalog_controller.cpp)
target_link_libraries(catalog-service PRIVATE service_main db_catalog bus)

add_executable(checkout-service src/services/checkout/main.cpp src/services/checkout/transactions_controller.cpp)
target_link_libraries(checkout-service PRIVATE service_main db_checkout bus)

add_executable(inventory-service src/services/inventory/main.cpp src/services/inventory/inventory_controller.cpp)
target_link_libraries(inventory-service PRIVATE service_main db_inventory bus)

add_executable(analytics-service src/services/analytics/main.cpp src/services/analytics/analytics_controller.cpp
    src/analytics/analytics.cpp)
target_link_libraries(analytics-service PRIVATE service_main db_analytics bus)

enable_testing()
add_subdirectory(tests)
