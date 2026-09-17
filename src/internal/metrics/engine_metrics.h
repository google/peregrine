#ifndef PEREGRINE_SRC_INTERNAL_METRICS_ENGINE_METRICS_H_
#define PEREGRINE_SRC_INTERNAL_METRICS_ENGINE_METRICS_H_

#include <cstdint>

#include "src/api/transport_metrics.h"
#include "src/internal/assumptions.h"
#include "src/internal/lib/metric_counter.h"

namespace peregrine::internal {

static_assert(assumptions::kRulesToFollowWhenAddingNewMetrics);

struct EngineMetrics final {
  // Total payload bytes sent across all data channels.
  MetricCounter<uint64_t> bytes_sent;
  // Total user transfer requests submitted.
  MetricCounter<uint64_t> requests_posted;
  // Total tcp connect failures.
  MetricCounter<uint64_t> tcp_connect_failures;

  // Takes a snapshot of the metrics.
  void Snapshot(TransportMetrics& m) const;
};

}  // namespace peregrine::internal

#endif  // PEREGRINE_SRC_INTERNAL_METRICS_ENGINE_METRICS_H_
