#include <trantor/utils/Logger.h>

#include "db/items.hpp"
#include "services.hpp"
#include "services/run_service.hpp"

int main(int argc, char** argv) {
    return runService(argc, argv, 8082, [](const drogon::orm::DbClientPtr& db, const AppConfig& cfg) {
        auto* s = new Services{   // lives for the whole process
            .config = cfg,
            .catalog = loadCatalog(db),
            .sales = Sales{db, cfg.lowStockThreshold},
            .scanLog = ScanLog{db},
        };
        // Same trick as the analytics persist filter: one INSERT per drain, so the batch grows with
        // DB latency instead of the backlog. This is the only thread writing scan_events.
        s->publisher = std::jthread([s] {
            for (auto batch = s->scans.popAll(); !batch.empty(); batch = s->scans.popAll()) {
                try {
                    s->scanLog.append(batch);
                } catch (const drogon::orm::DrogonDbException& e) {
                    LOG_ERROR << "scan_events insert (" << batch.size() << " scans) failed: " << e.base().what();
                }
            }
        });
        gServices.store(s);
    });
}
