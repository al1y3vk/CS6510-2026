#include <cstdlib>
#include <drogon/drogon.h>

#include "services.hpp"

// Usage: checkout-server [path/to/config.json]   (default ./config.json)
int main(int argc, char** argv) {
    auto& app = drogon::app();
    app.loadConfigFile(argc > 1 ? argv[1] : "config.json");

    app.registerBeginningAdvice([] {
        // Runs on the main loop once the DB client exists. The non-fast client has its own threads, so blocking here with execSqlSync is fine.
        auto db = drogon::app().getDbClient("default");
        try {
            AppConfig cfg = loadAppConfig(drogon::app().getCustomConfig());
            auto rows = db->execSqlSync("SELECT sku, id, name, price_cents FROM items ORDER BY id");
            if (rows.empty()) {
                LOG_ERROR << "catalog is empty — run layered/db/reset.sh";
                std::exit(1);
            }
            std::vector<Item> items;
            items.reserve(rows.size());
            for (const auto& row : rows) {
                items.push_back({row["id"].as<uint32_t>() - 1, row["sku"].as<std::string>(),
                                 row["name"].as<std::string>(), row["price_cents"].as<int64_t>()});
            }
            // Lives for the whole process; controllers read it through gServices.
            gServices.store(new Services{
                .config = cfg,
                .catalog = Catalog{std::move(items)},
                .transactions = Transactions{},
                .analytics = Analytics{cfg.windowSize, cfg.slideInterval, cfg.popularTopN, rows.size()},
                .inventory = Inventory{db, cfg.lowStockThreshold},
                .snapshots = Snapshots{db},
            });
            LOG_INFO << "catalog loaded: " << rows.size() << " items";
        } catch (const std::exception& e) {
            LOG_ERROR << "startup failed: " << e.what();
            std::exit(1);
        }
    });
    app.run();
}
