#pragma once

#include <chrono>
#include <cstdint>
#include <json/json.h>
#include <optional>
#include <string>
#include <vector>

class Catalog;

// What travels through the pipes, in pipeline order.
struct WindowCounts {                 // window -> rank
    uint64_t windowStart;
    uint64_t windowEnd;
    std::chrono::system_clock::time_point computedAt;
    std::vector<uint32_t> counts;     // scans per catalog index inside the window
};

struct PopularEntry { uint32_t index; uint32_t count; };

struct Snapshot {                     // rank -> format
    uint64_t windowStart;
    uint64_t windowEnd;
    std::chrono::system_clock::time_point computedAt;
    std::vector<PopularEntry> top;
};

struct SnapshotRow {                  // format -> persist: one popular_snapshots row
    uint64_t windowStart;
    uint64_t windowEnd;
    std::string computedAt;           // RFC 3339
    Json::Value items;                // array in /analytics/popular-items shape
};

// Filter 1: hopping window over the last `windowSize` scans. Emits a copy of the counts
// every `slideInterval` scans. Owned by one thread, so no lock.
class WindowFilter {
public:
    WindowFilter(size_t windowSize, size_t slideInterval, size_t catalogSize);
    std::optional<WindowCounts> record(uint32_t itemIndex);
private:
    uint64_t seq_ = 0;
    std::vector<uint32_t> ring_;
    std::vector<uint32_t> counts_;
    size_t windowSize_, slideInterval_;
};

// Filter 2: top `topN` by count desc, then index asc.
Snapshot rankTop(const WindowCounts& window, size_t topN);

// Filter 3: catalog indexes -> sku/name/scanCount/rank JSON, timestamp -> RFC 3339.
SnapshotRow formatSnapshot(const Snapshot& snapshot, const Catalog& catalog);
