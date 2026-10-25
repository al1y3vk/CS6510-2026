#include "http/peer.hpp"
#include "services.hpp"
#include "services/run_service.hpp"

int main(int argc, char** argv) {
    return runService(argc, argv, 8084, [](const drogon::orm::DbClientPtr& db, const AppConfig& cfg) {
        auto* s = new Services{   // lives for the whole process
            .config = cfg,
            .catalog = fetchCatalog(customString("catalog_url")),
            .analytics = Analytics{cfg.windowSize, cfg.slideInterval, cfg.popularTopN},
            .snapshots = Snapshots{db},
        };
        s->analytics.start(s->catalog, s->snapshots);
        gServices.store(s);
    });
}
