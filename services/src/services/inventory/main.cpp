#include "services.hpp"
#include "services/run_service.hpp"

int main(int argc, char** argv) {
    return runService(argc, argv, 8083, [](const drogon::orm::DbClientPtr& db, const AppConfig& cfg) {
        gServices.store(new Services{.config = cfg, .stock = Stock{db}});   // lives for the whole process
    });
}
