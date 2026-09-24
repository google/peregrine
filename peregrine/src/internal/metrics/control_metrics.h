#ifndef PEREGRINE_SRC_INTERNAL_METRICS_CONTROL_METRICS_H_
#define PEREGRINE_SRC_INTERNAL_METRICS_CONTROL_METRICS_H_

#include <cstdint>

#include "peregrine/src/api/transport_metrics.h"
#include "peregrine/src/internal/assumptions.h"
#include "peregrine/src/internal/lib/metric_counter.h"

namespace peregrine::internal {

static_assert(assumptions::kRulesToFollowWhenAddingNewMetrics);

struct ControlMetrics final {
  // Number of RPC requests received by the control plane.
  MetricCounter<uint64_t> rpc_requests_received;

  // Takes a snapshot of the metrics.
  void Snapshot(TransportMetrics& m) const;
};

}  // namespace peregrine::internal

#endif  // PEREGRINE_SRC_INTERNAL_METRICS_CONTROL_METRICS_H_
