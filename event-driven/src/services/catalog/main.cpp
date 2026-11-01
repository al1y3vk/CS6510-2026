#include "bus/bus.hpp"
#include "db/items.hpp"
#include "services.hpp"
#include "services/run_service.hpp"

int main(int argc, char** argv) {
    return runService(argc, argv, 8081, [](const drogon::orm::DbClientPtr& db, const AppConfig&) {
        auto* s = new Services{.catalog = loadCatalog(db)};   // lives for the whole process
        // The other services read the catalog from this event, not from GET /items. The topic is
        // compacted, so it stays there for services that start later.
        publishCatalog(customString("kafka_brokers"), s->catalog);
        gServices.store(s);
    });
}
