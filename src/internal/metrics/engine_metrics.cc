#include "src/internal/metrics/engine_metrics.h"

#include "src/api/transport_metrics.h"
#include "src/internal/assumptions.h"

namespace peregrine::internal {

static_assert(assumptions::kRulesToFollowWhenAddingNewMetrics);

void EngineMetrics::Snapshot(TransportMetrics& m) const {
  m.e2e_write_latency_us = e2e_write_latency_us.Snapshot();
  m.bytes_sent = bytes_sent.Value();
  m.request_write_size = request_write_size.Snapshot();
  m.request_read_size = request_read_size.Snapshot();
  m.e2e_write_errors = e2e_write_errors.Value();
  m.tcp_connect_failures = tcp_connect_failures.Value();
}

}  // namespace peregrine::internal
