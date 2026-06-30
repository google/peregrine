#ifndef PEREGRINE_SRC_API_TRANSPORT_UTIL_H_
#define PEREGRINE_SRC_API_TRANSPORT_UTIL_H_

#include <memory>
#include <string_view>

#include "src/api/transport.h"

namespace peregrine {

// Creates a new transport instance that listens on the tcp `endpoint`
// (e.g., "127.0.0.1:12345" or "[::1]:12345"). The `num_conns_per_peer`
// parameter guides the transport to make this number (in the range
// [1, 100]) of parallel connections between itself and each peer.
std::unique_ptr<Transport> CreateTransport(std::string_view endpoint,
                                           int num_conns_per_peer = 8);

}  // namespace peregrine

#endif  // PEREGRINE_SRC_API_TRANSPORT_UTIL_H_
