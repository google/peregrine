#ifndef PEREGRINE_SRC_API_OPTIONS_H_
#define PEREGRINE_SRC_API_OPTIONS_H_

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

  // Callback invoked on the receiver to resolve the target buffer address for
  // an incoming request when `Request::raddr` is not provided by the sender.
  // If `nullptr`, incoming requests with a null `raddr` are rejected.
  ResolveAddrCallback resolve_addr_callback = nullptr;
};

}  // namespace peregrine

#endif  // PEREGRINE_SRC_API_OPTIONS_H_
