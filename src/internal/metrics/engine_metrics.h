#ifndef PEREGRINE_SRC_INTERNAL_METRICS_ENGINE_METRICS_H_
#define PEREGRINE_SRC_INTERNAL_METRICS_ENGINE_METRICS_H_

#include <cstdint>

#include "src/api/transport_metrics.h"
#include "src/internal/assumptions.h"
#include "src/internal/lib/log2_histogram.h"
#include "src/internal/lib/metric_counter.h"

namespace peregrine::internal {

static_assert(assumptions::kRulesToFollowWhenAddingNewMetrics);

struct EngineMetrics final {
  // End-to-end write duration in microseconds.
  Log2Histogram<32> e2e_write_latency_us;
  // Total payload bytes sent across all data channels.
  MetricCounter<uint64_t> bytes_sent;
  // Write request per-op size.
  Log2Histogram<32> request_write_size;
  // Read request per-op size.
  Log2Histogram<32> request_read_size;
  // Total transfer write failures.
  MetricCounter<uint64_t> e2e_write_errors;
  // Total tcp connect failures.
  MetricCounter<uint64_t> tcp_connect_failures;

  // Takes a snapshot of the metrics.
  void Snapshot(TransportMetrics& m) const;
};

}  // namespace peregrine::internal

#endif  // PEREGRINE_SRC_INTERNAL_METRICS_ENGINE_METRICS_H_
