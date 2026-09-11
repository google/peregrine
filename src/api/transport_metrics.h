#ifndef PEREGRINE_SRC_API_TRANSPORT_METRICS_H_
#define PEREGRINE_SRC_API_TRANSPORT_METRICS_H_

#include <cstdint>

namespace peregrine {

// High-level end-to-end metrics for transport users.
struct TransportMetrics final {};

// Subsystem-level and hardware metrics for developers and diagnostics.
struct TransportMetricsDetails final {
  // End-to-end metrics.
  TransportMetrics e2e{};

  // Transport Pipeline Breakdown
  // ---------------------------------------------------------------------------
  // TODO(yyd): Add tier 2 metrics here

  // Hardware & Subsystem Internals
  // ---------------------------------------------------------------------------
  // Number of failed TCP connection attempts to peers
  uint64_t tcp_connect_failures = 0;

  // Number of RPC requests received by the control plane
  uint64_t rpc_requests_received = 0;
};

}  // namespace peregrine

#endif  // PEREGRINE_SRC_API_TRANSPORT_METRICS_H_
