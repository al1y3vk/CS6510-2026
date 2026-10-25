#pragma once

#include <cstdint>
#include <string>
#include <optional>
#include <vector>
#include <drogon/orm/DbClient.h>
#include <drogon/utils/coroutine.h>
#include <json/json.h>

struct SnapshotRow;   // analytics/filters.hpp

struct StoredSnapshot {
    uint64_t windowStart;
    uint64_t windowEnd;
    std::string computedAt;
    Json::Value items;
};

class Snapshots {
public:
    explicit Snapshots(drogon::orm::DbClientPtr db);
    // One INSERT (one commit) for the whole batch, rows keep their order. Blocking; called only
    // from the pipeline's persist thread, never an event loop.
    void insert(const std::vector<SnapshotRow>& rows);
    drogon::Task<std::optional<StoredSnapshot>> latest();
private:
    drogon::orm::DbClientPtr db_;
};
