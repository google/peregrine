#ifndef PEREGRINE_SRC_API_TRANSPORT_METRICS_H_
#define PEREGRINE_SRC_API_TRANSPORT_METRICS_H_

#include <cstdint>

namespace peregrine {

struct TransportMetrics final {
  // Number of failed TCP connection attempts to peers.
  uint64_t tcp_connect_failures = 0;

  // Number of RPC requests received by the control plane.
  uint64_t rpc_requests_received = 0;
};

}  // namespace peregrine

#endif  // PEREGRINE_SRC_API_TRANSPORT_METRICS_H_
