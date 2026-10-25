#include <chrono>
#include <trantor/utils/Logger.h>

#include "services.hpp"
#include "services/run_service.hpp"

namespace {
constexpr size_t kRelayBatch = 1000;
constexpr auto kRelayIdle = std::chrono::milliseconds(10);    // outbox empty: poll again after this
constexpr auto kRetry = std::chrono::milliseconds(200);       // peer down or DB error: retry after this
}  // namespace

int main(int argc, char** argv) {
    return runService(argc, argv, 8082, [](const drogon::orm::DbClientPtr& db, const AppConfig& cfg) {
        auto* s = new Services{   // lives for the whole process
            .config = cfg,
            .catalog = fetchCatalog(customString("catalog_url")),
            .sales = Sales{db},
            .outbox = SaleOutbox{db},
            .inventory = Peer{customString("inventory_url")},
            .analytics = Peer{customString("analytics_url")},
        };
        // Scans: one POST per drain, so the batch grows with analytics' latency instead of the
        // backlog. A failed POST is retried with the same batch; new scans wait in the pipe.
        s->publisher = std::jthread([s] {
            for (auto batch = s->scans.popAll(); !batch.empty(); batch = s->scans.popAll()) {
                Json::Value body;
                body["items"] = Json::Value(Json::arrayValue);
                for (uint32_t i : batch) body["items"].append(i);
                while (!s->analytics.post("/events/scans", body)) {
                    LOG_WARN << "analytics unreachable, retrying " << batch.size() << " scans";
                    std::this_thread::sleep_for(kRetry);
                }
            }
        });
        // Outbox relay: sale_outbox -> inventory. A row is deleted only after inventory answers 2xx,
        // so every sale reaches inventory at least once; inventory skips the ones it already applied.
        s->relay = std::jthread([s](std::stop_token stop) {
            while (!stop.stop_requested()) {
                try {
                    OutboxBatch batch = s->outbox.pending(kRelayBatch);
                    if (batch.ids.empty()) {
                        std::this_thread::sleep_for(kRelayIdle);
                        continue;
                    }
                    if (!s->inventory.post("/events/sales", saleEventsJson(batch.sales))) {
                        LOG_WARN << "inventory unreachable, retrying " << batch.ids.size() << " sales";
                        std::this_thread::sleep_for(kRetry);
                        continue;
                    }
                    s->outbox.remove(batch.ids);
                } catch (const drogon::orm::DrogonDbException& e) {
                    LOG_ERROR << "outbox relay: " << e.base().what();
                    std::this_thread::sleep_for(kRetry);
                }
            }
        });
        gServices.store(s);
    });
}
