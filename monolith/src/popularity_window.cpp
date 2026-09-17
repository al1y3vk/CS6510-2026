#include "popularity_window.hpp"

#include <algorithm>
#include <limits>

namespace {
constexpr uint32_t kEmpty = std::numeric_limits<uint32_t>::max();
}

PopularityWindow::PopularityWindow(size_t windowSize, size_t slideInterval, size_t topN, size_t catalogSize)
    : ring_(windowSize, kEmpty),
      counts_(catalogSize, 0),
      windowSize_(windowSize),
      slideInterval_(slideInterval),
      topN_(topN) {}

std::optional<Snapshot> PopularityWindow::record(uint32_t itemIndex) {
    std::lock_guard lock(mu_);
    ++seq_;
    uint32_t& slot = ring_[(seq_ - 1) % windowSize_];
    if (slot != kEmpty) --counts_[slot];
    slot = itemIndex;
    ++counts_[itemIndex];

    if (seq_ % slideInterval_ != 0) return std::nullopt;

    std::vector<PopularEntry> entries;
    for (uint32_t i = 0; i < counts_.size(); ++i)
        if (counts_[i] > 0) entries.push_back({i, counts_[i]});

    const size_t n = std::min(topN_, entries.size());
    std::partial_sort(entries.begin(), entries.begin() + n, entries.end(),
                      [](const PopularEntry& a, const PopularEntry& b) {
                          return a.count != b.count ? a.count > b.count : a.index < b.index;
                      });
    entries.resize(n);

    return Snapshot{
        .windowStart = seq_ >= windowSize_ ? seq_ - windowSize_ + 1 : 1,
        .windowEnd = seq_,
        .computedAt = std::chrono::system_clock::now(),
        .top = std::move(entries),
    };
}
