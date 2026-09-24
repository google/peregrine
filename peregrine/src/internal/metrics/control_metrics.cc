#include "peregrine/src/internal/metrics/control_metrics.h"

#include "peregrine/src/api/transport_metrics.h"
#include "peregrine/src/internal/assumptions.h"

namespace peregrine::internal {

static_assert(assumptions::kRulesToFollowWhenAddingNewMetrics);

void ControlMetrics::Snapshot(TransportMetrics& m) const {
  m.rpc_requests_received = rpc_requests_received.Value();
}

}  // namespace peregrine::internal
