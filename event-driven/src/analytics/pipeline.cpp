#include "analytics/pipeline.hpp"

#include "catalog.hpp"

namespace {
// One filter thread: apply `f` to every value from `in` until it closes, forward results
// to `out`, then close `out` so the next filter shuts down too.
template <class In, class Out, class F>
std::jthread filter(Pipe<In>& in, Pipe<Out>& out, F f) {
    return std::jthread([&in, &out, f = std::move(f)]() mutable {
        while (auto value = in.pop())
            if (std::optional<Out> result = f(std::move(*value))) out.push(std::move(*result));
        out.close();
    });
}
}  // namespace

AnalyticsPipeline::AnalyticsPipeline(size_t windowSize, size_t slideInterval, size_t topN,
                                     const Catalog& catalog, Sink sink)
    : window_(filter(scans_, windows_, [w = WindowFilter(windowSize, slideInterval, catalog.size())](uint32_t index) mutable {
          return w.record(index);
      })),
      rank_(filter(windows_, ranked_, [topN](const WindowCounts& counts) { return rankTop(counts, topN); })),
      format_(filter(ranked_, rows_, [&catalog](const Snapshot& snap) { return formatSnapshot(snap, catalog); })),
      persist_([this, sink = std::move(sink)] {
          for (auto batch = rows_.popAll(); !batch.empty(); batch = rows_.popAll()) sink(batch);
      }) {}

AnalyticsPipeline::~AnalyticsPipeline() {
    scans_.close();
}
