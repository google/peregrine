#include "src/api/transport_util.h"

#include <algorithm>
#include <memory>
#include <string_view>

#include "absl/base/optimization.h"
#include "absl/log/log.h"
#include "src/api/transport.h"
#include "src/internal/base/endpoint.h"
#include "src/internal/transport_impl.h"

namespace peregrine {

using internal::Endpoint;
using internal::TransportImpl;

std::unique_ptr<Transport> CreateTransport(std::string_view control_endpoint,
                                           int num_conns_per_peer) {
  const Endpoint ctrl_ep = Endpoint::Create(control_endpoint);
  if ABSL_PREDICT_FALSE (!ctrl_ep.HasNonzeroIpPort()) {
    LOG(WARNING) << "invalid control endpoint: " << control_endpoint;
    return nullptr;
  }

  const int n = std::min(std::max(1, num_conns_per_peer), 100);
  return TransportImpl::Create(ctrl_ep, n);
}

}  // namespace peregrine
