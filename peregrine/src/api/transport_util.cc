#include "peregrine/src/api/transport_util.h"

#include <algorithm>
#include <memory>
#include <string_view>
#include <utility>

#include "grpcpp/security/credentials.h"
#include "grpcpp/security/server_credentials.h"
#include "peregrine/src/api/transport.h"
#include "peregrine/src/api/transport_types.h"
#include "peregrine/src/internal/base/config.h"
#include "peregrine/src/internal/base/endpoint.h"
#include "peregrine/src/internal/transport_impl.h"

namespace peregrine {

using internal::Config;
using internal::Endpoint;
using internal::TransportImpl;

std::unique_ptr<Transport> CreateTransport(std::string_view endpoint,
                                           Options options) {
  const Endpoint e = Endpoint::Create(endpoint);

  const int n = std::min(std::max(1, options.num_parallel_connections), 100);
  const Config config = {
      .transport_type = options.transport_type,
      .num_conns_per_peer = n,
      .require_dataplane_encryption = false,
  };

  // TODO: Support passing in credentials.
  internal::SecurityCredentials creds = {
      .server_creds = grpc::InsecureServerCredentials(),
      .client_creds = grpc::InsecureChannelCredentials(),
  };

  return TransportImpl::Create(config, e, std::move(creds),
                               std::move(options.recv_post_callback));
}

std::unique_ptr<Transport> CreateTransport(std::string_view endpoint,
                                           const TransportType transport_type,
                                           const int num_conns_per_peer) {
  return CreateTransport(endpoint,
                         Options{
                             .transport_type = transport_type,
                             .num_parallel_connections = num_conns_per_peer,
                         });
}

}  // namespace peregrine
