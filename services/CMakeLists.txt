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

add_library(checkout_core STATIC
    src/money.cpp src/catalog.cpp src/transactions/transactions.cpp src/analytics/filters.cpp src/analytics/pipeline.cpp src/app_config.cpp)
target_include_directories(checkout_core PUBLIC src)
target_link_libraries(checkout_core PUBLIC Jsoncpp_lib)     # app_config reads Json::Value

# Shared DB access: one library per table group, so each service's link line below lists
# exactly the tables it touches. All of them talk to the same Postgres.
add_library(db_items STATIC src/db/items.cpp)               # items (read)
add_library(db_sales STATIC src/db/sales.cpp)               # sales, receipt_lines, stock via complete_sale()
add_library(db_stock STATIC src/db/stock.cpp)               # stock, items (read)
add_library(db_scans STATIC src/db/scan_log.cpp)            # scan_events
add_library(db_snapshots STATIC src/db/snapshots.cpp)       # popular_snapshots
foreach(lib db_items db_sales db_stock db_scans db_snapshots)
    target_link_libraries(${lib} PUBLIC checkout_core Drogon::Drogon)
endforeach()

add_library(service_main STATIC src/services/run_service.cpp)   # config + port + startup, same for all four
target_link_libraries(service_main PUBLIC checkout_core Drogon::Drogon)

add_executable(catalog-service src/services/catalog/main.cpp src/services/catalog/catalog_controller.cpp)
target_link_libraries(catalog-service PRIVATE service_main db_items)

add_executable(checkout-service src/services/checkout/main.cpp src/services/checkout/transactions_controller.cpp)
target_link_libraries(checkout-service PRIVATE service_main db_items db_sales db_scans)

add_executable(inventory-service src/services/inventory/main.cpp src/services/inventory/inventory_controller.cpp)
target_link_libraries(inventory-service PRIVATE service_main db_stock)

add_executable(analytics-service
    src/services/analytics/main.cpp src/services/analytics/analytics_controller.cpp src/analytics/analytics.cpp)
target_link_libraries(analytics-service PRIVATE service_main db_items db_scans db_snapshots)

enable_testing()
add_subdirectory(tests)
