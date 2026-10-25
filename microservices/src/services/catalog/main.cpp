#include "db/items.hpp"
#include "services.hpp"
#include "services/run_service.hpp"

int main(int argc, char** argv) {
    return runService(argc, argv, 8081, [](const drogon::orm::DbClientPtr& db, const AppConfig&) {
        gServices.store(new Services{.catalog = loadCatalog(db)});   // lives for the whole process
    });
}
