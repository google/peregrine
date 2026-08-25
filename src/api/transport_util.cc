#include "src/api/transport_util.h"

#include <algorithm>
#include <memory>
#include <string_view>

#include "absl/base/optimization.h"
#include "src/api/transport.h"
#include "src/internal/base/config.h"
#include "src/internal/base/endpoint.h"
#include "src/internal/transport_impl.h"

namespace peregrine {

using internal::Config;
using internal::Endpoint;
using internal::TransportImpl;

std::unique_ptr<Transport> CreateTransport(std::string_view endpoint,
                                           int num_conns_per_peer) {
  const Endpoint e = Endpoint::Create(endpoint);
  if ABSL_PREDICT_FALSE (!e.HasNonzeroIpPort()) {
    return nullptr;
  }

  const int n = std::min(std::max(1, num_conns_per_peer), 100);
  const Config config = {.num_conns_per_peer = n};
  return TransportImpl::Create(config, e);
}

}  // namespace peregrine
