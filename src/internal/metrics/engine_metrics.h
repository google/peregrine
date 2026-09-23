#ifndef PEREGRINE_SRC_INTERNAL_METRICS_ENGINE_METRICS_H_
#define PEREGRINE_SRC_INTERNAL_METRICS_ENGINE_METRICS_H_

#include <cstdint>

#include "src/api/transport_metrics.h"
#include "src/internal/assumptions.h"
#include "src/internal/lib/log2_histogram.h"
#include "src/internal/lib/metric_counter.h"

namespace peregrine::internal {

static_assert(assumptions::kRulesToFollowWhenAddingNewMetrics);

struct OpMetrics final {
  // End-to-end transfer duration in microseconds.
  Log2Histogram<32> e2e_latency_us;
  // Request per-op size in bytes.
  Log2Histogram<32> request_size_bytes;
  // Total payload bytes transferred across all data channels.
  MetricCounter<uint64_t> bytes;
  // Total transfer failures.
  MetricCounter<uint64_t> errors;

  // Takes a snapshot of the per-op metrics.
  ::peregrine::OpMetrics Snapshot() const;
};

struct EngineMetrics final {
  OpMetrics write;
  OpMetrics read;
  // Total tcp connect failures.
  MetricCounter<uint64_t> tcp_connect_failures;

  // Takes a snapshot of the metrics.
  void Snapshot(TransportMetrics& m) const;
};

}  // namespace peregrine::internal

#endif  // PEREGRINE_SRC_INTERNAL_METRICS_ENGINE_METRICS_H_
