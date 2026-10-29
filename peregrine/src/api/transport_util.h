#ifndef PEREGRINE_SRC_API_TRANSPORT_UTIL_H_
#define PEREGRINE_SRC_API_TRANSPORT_UTIL_H_

#include <memory>
#include <string_view>

#include "peregrine/src/api/transport.h"
#include "peregrine/src/api/transport_types.h"

namespace peregrine {

// Options for creating a transport instance.
struct Options {
  // Underlying data plane transport (TCP or RDMA).
  TransportType transport_type = TransportType::kAny;

  // The #connections to maintain b/t this process and each peer (in [1, 100]).
  int num_parallel_connections = 8;

  // Callback invoked on the receiver when an incoming `Post()` batch completes.
  // If `nullptr`, no callback is invoked on post receive completion.
  RecvPostCallback recv_post_callback = nullptr;
};

// Creates a new transport instance that listens for control messages on
// the control `endpoint` (e.g., "10.0.0.1:10000") using the given `options`.
// If the port is zero, the kernel assigns a free port.
//
// TODO: Support passing in both server and client gRPC credentials instead of
// hardcoding insecure credentials.
std::unique_ptr<Transport> CreateTransport(std::string_view endpoint,
                                           Options options = {});

}  // namespace peregrine

#endif  // PEREGRINE_SRC_API_TRANSPORT_UTIL_H_
