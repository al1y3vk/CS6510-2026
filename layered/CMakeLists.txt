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
    src/money.cpp src/catalog.cpp src/transactions/transactions.cpp src/analytics/popularity_window.cpp src/app_config.cpp)
target_include_directories(checkout_core PUBLIC src)
target_link_libraries(checkout_core PUBLIC Jsoncpp_lib)     # app_config reads Json::Value

add_library(checkout_db STATIC src/db/inventory.cpp src/db/snapshots.cpp src/analytics/analytics.cpp)
target_link_libraries(checkout_db PUBLIC checkout_core Drogon::Drogon)

add_executable(checkout-server
    src/main.cpp
    src/api/catalog_controller.cpp
    src/api/transactions_controller.cpp
    src/api/inventory_controller.cpp
    src/api/analytics_controller.cpp)
target_link_libraries(checkout-server PRIVATE checkout_db Drogon::Drogon)

enable_testing()
add_subdirectory(tests)
