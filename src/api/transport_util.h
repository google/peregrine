#ifndef PEREGRINE_SRC_API_TRANSPORT_UTIL_H_
#define PEREGRINE_SRC_API_TRANSPORT_UTIL_H_

#include <memory>

#include "src/api/transport.h"
#include "src/internal/transport_impl.h"

namespace peregrine {

// Creates a new transport instance.
inline std::unique_ptr<Transport> CreateTransport() {
  return std::make_unique<internal::TransportImpl>();
}

}  // namespace peregrine

#endif  // PEREGRINE_SRC_API_TRANSPORT_UTIL_H_
