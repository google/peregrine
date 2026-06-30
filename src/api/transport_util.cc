#include "src/api/transport_util.h"

#include <algorithm>
#include <memory>
#include <string_view>
#include <utility>

#include "absl/base/optimization.h"
#include "absl/log/log.h"
#include "src/api/transport.h"
#include "src/internal/base/endpoint.h"
#include "src/internal/socket/acceptor.h"
#include "src/internal/transport_impl.h"

namespace peregrine {

using internal::Endpoint;
using internal::TcpAcceptor;
using internal::TransportImpl;

// Creates a new transport instance.
std::unique_ptr<Transport> CreateTransport(std::string_view endpoint,
                                           int num_conns_per_peer) {
  const Endpoint self = Endpoint::Create(endpoint);
  if ABSL_PREDICT_FALSE (!self.IsValid()) {
    LOG(WARNING) << "failed to parse: " << endpoint;
    return nullptr;
  }

  std::unique_ptr<TcpAcceptor> acceptor = TcpAcceptor::Create(self);
  if ABSL_PREDICT_FALSE (acceptor == nullptr) {
    LOG(WARNING) << "failed to create acceptor: " << self;
    return nullptr;
  }

  const int n = std::min(std::max(1, num_conns_per_peer), 100);
  return std::make_unique<TransportImpl>(std::move(acceptor), self, n);
}

}  // namespace peregrine
