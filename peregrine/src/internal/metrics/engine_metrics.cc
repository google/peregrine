#include "peregrine/src/internal/metrics/engine_metrics.h"

#include "peregrine/src/api/transport_metrics.h"
#include "peregrine/src/internal/assumptions.h"

namespace peregrine::internal {

static_assert(assumptions::kRulesToFollowWhenAddingNewMetrics);

::peregrine::OpMetrics OpMetrics::Snapshot() const {
  return {
      .e2e_latency_us = e2e_latency_us.Snapshot(),
      .request_size_bytes = request_size_bytes.Snapshot(),
      .bytes = bytes.Value(),
      .errors = errors.Value(),
  };
}

void EngineMetrics::Snapshot(TransportMetrics& m) const {
  m.write = write.Snapshot();
  m.read = read.Snapshot();
  m.tcp_connect_failures = tcp_connect_failures.Value();
}

}  // namespace peregrine::internal
