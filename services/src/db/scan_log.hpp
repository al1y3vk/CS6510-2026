#pragma once

#include <cstdint>
#include <vector>
#include <drogon/orm/DbClient.h>

struct ScanEvent {
    int64_t id;
    uint32_t itemIndex;
};

// scan_events: checkout appends, analytics reads. Both calls block; run them off the HTTP threads.
class ScanLog {
public:
    explicit ScanLog(drogon::orm::DbClientPtr db);
    // The whole batch in one INSERT, ids in the given order.
    void append(const std::vector<uint32_t>& itemIndices);
    // Up to `limit` scans with id > afterId, oldest first.
    std::vector<ScanEvent> after(int64_t afterId, size_t limit);
private:
    drogon::orm::DbClientPtr db_;
};
