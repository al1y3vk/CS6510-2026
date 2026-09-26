#pragma once

#include <cstdint>
#include <string>
#include <optional>
#include <drogon/orm/DbClient.h>
#include <drogon/utils/coroutine.h>
#include <json/json.h>

struct Snapshot;   // analytics/popularity_window.hpp
class Catalog;     // catalog.hpp

struct StoredSnapshot {
    uint64_t windowStart;
    uint64_t windowEnd;
    std::string computedAt;
    Json::Value items;
};

class Snapshots {
public:
    explicit Snapshots(drogon::orm::DbClientPtr db);
    // Fire-and-forget: uses execSqlAsync; on error logs and drops. Never awaited by a scan.
    void insertAsync(const Snapshot& snapshot, const Catalog& catalog);
    drogon::Task<std::optional<StoredSnapshot>> latest();
private:
    drogon::orm::DbClientPtr db_;
};
