#ifndef PEREGRINE_SRC_API_TRANSPORT_UTIL_H_
#define PEREGRINE_SRC_API_TRANSPORT_UTIL_H_

#include <memory>
#include <string_view>

#include "src/api/transport.h"

namespace peregrine {

// Creates a new transport instance that listens on the tcp `endpoint`
// (e.g., "127.0.0.1:12345" or "[::1]:12345").
std::unique_ptr<Transport> CreateTransport(std::string_view endpoint);

}  // namespace peregrine

#endif  // PEREGRINE_SRC_API_TRANSPORT_UTIL_H_
