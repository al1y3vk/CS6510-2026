#include <chrono>
#include <drogon/utils/coroutine.h>
#include <trantor/utils/Logger.h>

#include "bus/bus.hpp"
#include "events.hpp"
#include "services.hpp"
#include "services/run_service.hpp"

namespace {
constexpr size_t kBatch = 1000;
constexpr auto kWait = std::chrono::milliseconds(100);    // longest wait for the first event of a batch
constexpr auto kRetry = std::chrono::milliseconds(200);   // DB error: retry the same batch after this
}  // namespace

int main(int argc, char** argv) {
    return runService(argc, argv, 8083, [](const drogon::orm::DbClientPtr& db, const AppConfig& cfg) {
        const std::string brokers = customString("kafka_brokers");
        auto* s = new Services{   // lives for the whole process
            .config = cfg,
            .catalog = fetchCatalog(brokers),
            .stock = Stock{db, cfg.lowStockThreshold},
        };
        // sales.completed -> stock. The offset is committed only after apply_sales has committed,
        // so a crash in between redelivers the batch, and applied_sales skips the sales already
        // applied: at-least-once delivery, exactly-once effect.
        s->consumer = std::jthread([s, brokers](std::stop_token stop) {
            Consumer events(brokers, "inventory", topic::kSales);
            while (!stop.stop_requested()) {
                const auto values = events.poll(kBatch, kWait);
                if (values.empty()) continue;
                std::vector<SaleEvent> sales;
                for (const std::string& v : values) {
                    if (auto sale = parseSaleEvent(v)) sales.push_back(std::move(*sale));
                    else LOG_ERROR << "skipping malformed sale event: " << v;
                }
                while (!sales.empty()) {
                    try {
                        drogon::sync_wait(s->stock.applySales(sales));
                        break;
                    } catch (const drogon::orm::DrogonDbException& e) {
                        LOG_ERROR << "apply_sales (" << sales.size() << " sales) failed: " << e.base().what();
                        std::this_thread::sleep_for(kRetry);
                    }
                }
                events.commit();
            }
        });
        gServices.store(s);
    });
}
