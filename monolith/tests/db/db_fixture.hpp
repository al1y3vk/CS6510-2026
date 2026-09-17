#pragma once
// Shared by the db tests: a tiny freshly-reset database (5 items, 3 stock each)
// and a raw client for asserting on table contents.
#include <cstdlib>
#include <drogon/orm/DbClient.h>
#include <memory>
#include <string>

inline const char* pgUrl() {
    const char* env = std::getenv("PGURL");
    return env ? env : "postgresql://checkout:checkout@localhost:5432/checkout";
}

struct TinyDb {
    drogon::orm::DbClientPtr client;

    TinyDb() {
        const std::string cmd = std::string("CATALOG_SIZE=5 STOCK_PER_ITEM=3 ") + MONOLITH_DIR + "/db/reset.sh >/dev/null";
        if (std::system(cmd.c_str()) != 0) throw std::runtime_error("db/reset.sh failed");
        client = drogon::orm::DbClient::newPgClient(pgUrl(), 2);
    }

    // Convenience for one-cell lookups in assertions.
    template <typename T>
    T scalar(const std::string& sql) {
        return client->execSqlSync(sql)[0][0].template as<T>();
    }
};
