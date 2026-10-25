#pragma once

#include <cstdint>
#include <functional>
#include <thread>

#include "analytics/filters.hpp"
#include "analytics/pipe.hpp"

class Catalog;

// scans ─► window ─► rank ─► format ─► persist(sink, batched)
// Each filter is one thread reading its input pipe; pipes are Pipe<T> blocking queues.
class AnalyticsPipeline {
public:
    using Sink = std::function<void(const std::vector<SnapshotRow>&)>;

    // `catalog` must outlive the pipeline. `sink` runs on the persist thread and gets every row
    // queued since its last call, in window order, so a slow sink gets bigger batches, not lag.
    AnalyticsPipeline(size_t windowSize, size_t slideInterval, size_t topN, const Catalog& catalog, Sink sink);
    // Closes the scan pipe; each filter drains, closes its output, and exits. Joins all four.
    ~AnalyticsPipeline();
    AnalyticsPipeline(const AnalyticsPipeline&) = delete;
    AnalyticsPipeline& operator=(const AnalyticsPipeline&) = delete;

    // Called from HTTP threads; never blocks.
    void submit(uint32_t itemIndex) { scans_.push(itemIndex); }
private:
    Pipe<uint32_t> scans_;
    Pipe<WindowCounts> windows_;
    Pipe<Snapshot> ranked_;
    Pipe<SnapshotRow> rows_;
    std::jthread window_, rank_, format_, persist_;   // after the pipes: joined before they are destroyed
};
