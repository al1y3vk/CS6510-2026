#pragma once
// Shared by the db tests: tiny freshly-reset databases (5 items, 3 stock each) and a raw client
// on one service's database for asserting on table contents.
#include <cstdlib>
#include <drogon/orm/DbClient.h>
#include <memory>
#include <string>

inline std::string pgUrl(const std::string& db) {
    const char* env = std::getenv("PGSERVER");
    return std::string(env ? env : "postgresql://checkout:checkout@localhost:5432") + "/" + db;
}

struct TinyDb {
    drogon::orm::DbClientPtr client;

    explicit TinyDb(const std::string& db) {
        const std::string cmd = std::string("CATALOG_SIZE=5 STOCK_PER_ITEM=3 ") + MONOLITH_DIR + "/db/reset.sh >/dev/null";
        if (std::system(cmd.c_str()) != 0) throw std::runtime_error("db/reset.sh failed");
        client = drogon::orm::DbClient::newPgClient(pgUrl(db), 2);
    }

    // Convenience for one-cell lookups in assertions.
    template <typename T>
    T scalar(const std::string& sql) {
        return client->execSqlSync(sql)[0][0].template as<T>();
    }
};
