#include <chrono>
#include <trantor/utils/Logger.h>

#include "db/items.hpp"
#include "services.hpp"
#include "services/run_service.hpp"

namespace {
constexpr size_t kPollBatch = 10'000;
constexpr auto kPollIdle = std::chrono::milliseconds(50);   // sleep only when caught up
}  // namespace

int main(int argc, char** argv) {
    return runService(argc, argv, 8084, [](const drogon::orm::DbClientPtr& db, const AppConfig& cfg) {
        auto* s = new Services{   // lives for the whole process
            .config = cfg,
            .catalog = loadCatalog(db),
            .analytics = Analytics{cfg.windowSize, cfg.slideInterval, cfg.popularTopN},
            .snapshots = Snapshots{db},
            .scanLog = ScanLog{db},
        };
        s->analytics.start(s->catalog, s->snapshots);
        s->poller = std::jthread([s](std::stop_token stop) {
            int64_t last = 0;   // a restart replays scan_events from the beginning
            while (!stop.stop_requested()) {
                std::vector<ScanEvent> batch;
                try {
                    batch = s->scanLog.after(last, kPollBatch);
                } catch (const drogon::orm::DrogonDbException& e) {
                    LOG_ERROR << "scan_events read failed: " << e.base().what();
                }
                for (const ScanEvent& e : batch) s->analytics.recordScan(e.itemIndex);
                if (!batch.empty()) last = batch.back().id;
                if (batch.size() < kPollBatch) std::this_thread::sleep_for(kPollIdle);
            }
        });
        gServices.store(s);
    });
}
