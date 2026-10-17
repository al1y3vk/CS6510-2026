#include "analytics/filters.hpp"

#include <algorithm>
#include <limits>

#include "catalog.hpp"
#include "money.hpp"

namespace {
constexpr uint32_t kEmpty = std::numeric_limits<uint32_t>::max();
}

WindowFilter::WindowFilter(size_t windowSize, size_t slideInterval, size_t catalogSize)
    : ring_(windowSize, kEmpty), counts_(catalogSize, 0), windowSize_(windowSize), slideInterval_(slideInterval) {}

std::optional<WindowCounts> WindowFilter::record(uint32_t itemIndex) {
    ++seq_;
    uint32_t& slot = ring_[(seq_ - 1) % windowSize_];
    if (slot != kEmpty) --counts_[slot];
    slot = itemIndex;
    ++counts_[itemIndex];

    if (seq_ % slideInterval_ != 0) return std::nullopt;
    return WindowCounts{
        .windowStart = seq_ >= windowSize_ ? seq_ - windowSize_ + 1 : 1,
        .windowEnd = seq_,
        .computedAt = std::chrono::system_clock::now(),
        .counts = counts_,
    };
}

Snapshot rankTop(const WindowCounts& window, size_t topN) {
    std::vector<PopularEntry> entries;
    for (uint32_t i = 0; i < window.counts.size(); ++i)
        if (window.counts[i] > 0) entries.push_back({i, window.counts[i]});

    const size_t n = std::min(topN, entries.size());
    std::partial_sort(entries.begin(), entries.begin() + n, entries.end(),
                      [](const PopularEntry& a, const PopularEntry& b) {
                          return a.count != b.count ? a.count > b.count : a.index < b.index;
                      });
    entries.resize(n);

    return Snapshot{
        .windowStart = window.windowStart,
        .windowEnd = window.windowEnd,
        .computedAt = window.computedAt,
        .top = std::move(entries),
    };
}

SnapshotRow formatSnapshot(const Snapshot& snapshot, const Catalog& catalog) {
    Json::Value items(Json::arrayValue);
    for (size_t rank = 0; rank < snapshot.top.size(); ++rank) {
        const Item& item = catalog.at(snapshot.top[rank].index);
        Json::Value entry;
        entry["sku"] = item.sku;
        entry["name"] = item.name;
        entry["scanCount"] = snapshot.top[rank].count;
        entry["rank"] = static_cast<Json::UInt>(rank + 1);
        items.append(std::move(entry));
    }
    return SnapshotRow{
        .windowStart = snapshot.windowStart,
        .windowEnd = snapshot.windowEnd,
        .computedAt = rfc3339(snapshot.computedAt),
        .items = std::move(items),
    };
}
