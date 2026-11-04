#include "peregrine/src/api/transport_util.h"

#include <algorithm>
#include <memory>
#include <string_view>
#include <utility>

#include "grpcpp/security/credentials.h"
#include "grpcpp/security/server_credentials.h"
#include "peregrine/src/api/transport.h"
#include "peregrine/src/api/transport_registry.h"
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
  const int n = std::min(std::max(1, options.num_parallel_connections), 100);

  // TEMPORARY: a registered transport may stand in for RDMA until it is
  // integrated into Peregrine; see transport_registry.h.
  if (options.transport_type == TransportType::kRdma) {
    if (const TransportFactory* factory =
            internal::GetRegisteredRdmaTransport();
        factory != nullptr) {
      options.num_parallel_connections = n;
      return (*factory)(endpoint, std::move(options));
    }
  }

  const Endpoint e = Endpoint::Create(endpoint);
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
                               std::move(options.recv_post_callback),
                               std::move(options.resolve_addr_callback));
}

}  // namespace peregrine
