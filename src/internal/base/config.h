#ifndef PEREGRINE_SRC_INTERNAL_BASE_CONFIG_H_
#define PEREGRINE_SRC_INTERNAL_BASE_CONFIG_H_

#include <memory>

#include "grpcpp/security/credentials.h"
#include "grpcpp/security/server_credentials.h"
#include "src/internal/assumptions.h"

namespace peregrine::internal {

// This struct holds the configuration for the transport.
// It is thread-safe since it's read-only after construction.
struct Config {
  // The number of connections to maintain per peer.
  int num_conns_per_peer = 1;

  // Whether to require encryption on data plane connections.
  // Set to false when network is encrypted, e.g. in GCP.
  static_assert(assumptions::kChunkHeaderAndPayloadAreEncryptedOnWire);
  bool require_dataplane_encryption = false;

  // Returns true iff the config is valid.
  bool IsValid() const {
    return 1 <= num_conns_per_peer && num_conns_per_peer <= 100;
  }
};

// This struct holds the security credentials for the transport.
struct SecurityCredentials {
  std::shared_ptr<grpc::ServerCredentials> server_creds =
      grpc::InsecureServerCredentials();

  std::shared_ptr<grpc::ChannelCredentials> client_creds =
      grpc::InsecureChannelCredentials();

  // Returns true iff the security credentials are valid.
  bool IsValid() const {
    return server_creds != nullptr && client_creds != nullptr;
  }
};

}  // namespace peregrine::internal

#endif  // PEREGRINE_SRC_INTERNAL_BASE_CONFIG_H_
