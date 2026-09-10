#ifndef PEREGRINE_SRC_INTERNAL_METRICS_CONTROL_METRICS_H_
#define PEREGRINE_SRC_INTERNAL_METRICS_CONTROL_METRICS_H_

#include <cstdint>

#include "src/api/transport_types.h"
#include "src/internal/assumptions.h"
#include "src/internal/lib/metric_counter.h"

namespace peregrine::internal {

struct ControlMetrics final {
  static_assert(assumptions::kRulesToFollowWhenAddingNewMetrics);

  // Number of RPC requests received by the control plane.
  MetricCounter<uint64_t> rpc_requests_received;

  void Snapshot(TransportMetrics& m) const {
    m.rpc_requests_received = rpc_requests_received.Value();
  }
};

}  // namespace peregrine::internal

#endif  // PEREGRINE_SRC_INTERNAL_METRICS_CONTROL_METRICS_H_
