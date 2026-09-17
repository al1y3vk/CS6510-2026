#pragma once

#include <chrono>
#include <cstdint>
#include <mutex>
#include <optional>
#include <vector>

struct PopularEntry { uint32_t index; uint32_t count; };

struct Snapshot {
    uint64_t windowStart;
    uint64_t windowEnd;
    std::chrono::system_clock::time_point computedAt;
    std::vector<PopularEntry> top;
};

class PopularityWindow {
public:
    PopularityWindow(size_t windowSize, size_t slideInterval, size_t topN, size_t catalogSize);
    std::optional<Snapshot> record(uint32_t itemIndex); // one scan; snapshot iff seq % slideInterval == 0
private:
    std::mutex mu_;
    uint64_t seq_ = 0;
    std::vector<uint32_t> ring_;
    std::vector<uint32_t> counts_;
    size_t windowSize_, slideInterval_, topN_;   // config copies
};