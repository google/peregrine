#ifndef PEREGRINE_SRC_API_TRANSPORT_UTIL_H_
#define PEREGRINE_SRC_API_TRANSPORT_UTIL_H_

#include <memory>
#include <string_view>

#include "peregrine/src/api/options.h"
#include "peregrine/src/api/transport.h"

namespace peregrine {

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
