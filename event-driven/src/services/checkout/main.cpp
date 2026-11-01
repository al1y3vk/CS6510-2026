#include <chrono>
#include <trantor/utils/Logger.h>

#include "events.hpp"
#include "services.hpp"
#include "services/run_service.hpp"

namespace {
constexpr size_t kRelayBatch = 1000;
constexpr auto kRelayIdle = std::chrono::milliseconds(10);    // outbox empty: poll again after this
constexpr auto kRetry = std::chrono::milliseconds(200);       // broker or DB error: retry after this
constexpr auto kAckTimeout = std::chrono::seconds(10);        // wait this long for the broker to store a batch
}  // namespace

int main(int argc, char** argv) {
    return runService(argc, argv, 8082, [](const drogon::orm::DbClientPtr& db, const AppConfig& cfg) {
        const std::string brokers = customString("kafka_brokers");
        auto* s = new Services{   // lives for the whole process
            .config = cfg,
            .catalog = fetchCatalog(brokers),
            .sales = Sales{db},
            .outbox = SaleOutbox{db},
            .scanEvents = Producer{brokers},
            .saleEvents = Producer{brokers},
        };
        // Outbox relay: sale_outbox -> sales.completed. Rows are deleted only after the broker has
        // stored their events, so every sale is published at least once; inventory skips repeats.
        s->relay = std::jthread([s](std::stop_token stop) {
            while (!stop.stop_requested()) {
                try {
                    OutboxBatch batch = s->outbox.pending(kRelayBatch);
                    if (batch.ids.empty()) {
                        std::this_thread::sleep_for(kRelayIdle);
                        continue;
                    }
                    bool queued = true;
                    for (const SaleEvent& sale : batch.sales)
                        queued = s->saleEvents.publish(topic::kSales, sale.transactionId, saleEventJson(sale)) && queued;
                    if (!s->saleEvents.flush(kAckTimeout) || !queued) {
                        LOG_WARN << "kafka did not store " << batch.ids.size() << " sale events, retrying";
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
