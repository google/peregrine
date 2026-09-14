#include "src/internal/metrics/engine_metrics.h"

#include "src/api/transport_metrics.h"
#include "src/internal/assumptions.h"

namespace peregrine::internal {

static_assert(assumptions::kRulesToFollowWhenAddingNewMetrics);

void EngineMetrics::Snapshot(TransportMetrics& m) const {
  m.bytes_sent = bytes_sent.Value();
}

void EngineMetrics::SnapshotDetails(TransportMetricsDetails& m) const {
  m.tcp_connect_failures = tcp_connect_failures.Value();
}

}  // namespace peregrine::internal
