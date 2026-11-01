#include <chrono>
#include <trantor/utils/Logger.h>

#include "bus/bus.hpp"
#include "events.hpp"
#include "services.hpp"
#include "services/run_service.hpp"

namespace {
constexpr size_t kBatch = 10000;
constexpr auto kWait = std::chrono::milliseconds(100);   // longest wait for the first event of a batch
}  // namespace

int main(int argc, char** argv) {
    return runService(argc, argv, 8084, [](const drogon::orm::DbClientPtr& db, const AppConfig& cfg) {
        const std::string brokers = customString("kafka_brokers");
        auto* s = new Services{   // lives for the whole process
            .config = cfg,
            .catalog = fetchCatalog(brokers),
            .analytics = Analytics{cfg.windowSize, cfg.slideInterval, cfg.popularTopN},
            .snapshots = Snapshots{db},
        };
        s->analytics.start(s->catalog, s->snapshots);
        // items.scanned -> pipeline. Committed after each batch is handed to the pipeline; after a
        // crash the uncommitted scans are read again (at least once; a repeat only nudges a window).
        s->consumer = std::jthread([s, brokers](std::stop_token stop) {
            Consumer events(brokers, "analytics", topic::kScans);
            while (!stop.stop_requested()) {
                const auto values = events.poll(kBatch, kWait);
                if (values.empty()) continue;
                for (const std::string& v : values) {
                    auto scan = parseScanEvent(v);
                    const Item* item = scan ? s->catalog.find(scan->sku) : nullptr;
                    if (item) s->analytics.recordScan(item->index);
                    else LOG_ERROR << "skipping bad scan event: " << v;
                }
                events.commit();
            }
        });
        gServices.store(s);
    });
}
