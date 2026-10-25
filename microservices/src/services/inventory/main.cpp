#include "http/peer.hpp"
#include "services.hpp"
#include "services/run_service.hpp"

int main(int argc, char** argv) {
    return runService(argc, argv, 8083, [](const drogon::orm::DbClientPtr& db, const AppConfig& cfg) {
        gServices.store(new Services{   // lives for the whole process
            .config = cfg,
            .catalog = fetchCatalog(customString("catalog_url")),
            .stock = Stock{db, cfg.lowStockThreshold},
        });
    });
}
