#include "src/api/transport_util.h"

#include <memory>
#include <string_view>

#include "absl/base/optimization.h"
#include "absl/log/log.h"
#include "src/api/transport.h"
#include "src/internal/base/endpoint.h"
#include "src/internal/transport_impl.h"

namespace peregrine {

using internal::Endpoint;

// Creates a new transport instance.
std::unique_ptr<Transport> CreateTransport(std::string_view endpoint) {
  const Endpoint local = Endpoint::Create(endpoint);
  if ABSL_PREDICT_FALSE (!local.IsValid()) {
    LOG(WARNING) << "failed to parse: " << endpoint;
    return nullptr;
  }
  return std::make_unique<internal::TransportImpl>(local);
}

}  // namespace peregrine
