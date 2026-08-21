#ifndef PEREGRINE_SRC_INTERNAL_CONTROL_CONTROL_H_
#define PEREGRINE_SRC_INTERNAL_CONTROL_CONTROL_H_

#include <memory>
#include <utility>

#include "absl/base/thread_annotations.h"
#include "absl/container/flat_hash_map.h"
#include "absl/log/check.h"
#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/synchronization/mutex.h"
#include "grpcpp/security/credentials.h"
#include "grpcpp/security/server_credentials.h"
#include "src/internal/base/endpoint.h"
#include "src/internal/base/hostinfo.h"
#include "src/internal/control/grpc_client.h"
#include "src/internal/control/grpc_server.h"
#include "src/internal/control/message.pb.h"
#include "src/util/macro.h"

namespace peregrine::internal {

// This class implements the transport control plane for endpoints to exchange
// request and response control messages.
// It is thread-safe.
class Control final {
 public:
  // Creates a control plane instance.
  static std::unique_ptr<Control> Create(
      const HostInfo& self,
      std::shared_ptr<grpc::ServerCredentials> server_creds,
      std::shared_ptr<grpc::ChannelCredentials> client_creds);

  // Disallows copy and move.
  DISALLOW_COPY(Control);
  DISALLOW_MOVE(Control);

  // Destructor.
  ~Control();

  // Starts the gRPC server to begin accepting incoming control requests.
  // Must be called only after host topology is ready to be served.
  bool Start();

  // Synchronously sends a request message to a remote peer endpoint.
  // Returns the response message or an error status.
  absl::StatusOr<proto::RespMsg> SendRequest(const Endpoint& peer,
                                             const proto::ReqMsg& req);

  // Resolves the physical multi-NIC HostInfo topology of a remote peer.
  absl::StatusOr<HostInfo> ResolvePeerHostInfo(const Endpoint& peer_control_ep)
      ABSL_LOCKS_EXCLUDED(peer_hosts_mu_);

 private:
  // Constructor.
  Control(const HostInfo& self,
          std::shared_ptr<grpc::ServerCredentials> server_creds,
          std::shared_ptr<grpc::ChannelCredentials> client_creds)
      : self_(self),
        server_creds_(std::move(server_creds)),
        client_creds_(std::move(client_creds)) {
    DCHECK(self_.control_plane_listener.HasNonzeroIpPort());
    DCHECK_NE(server_creds_, nullptr);
    DCHECK_NE(client_creds_, nullptr);
  }
  // Handles incoming RPC requests by dispatching to dedicated message handlers.
  absl::Status handleIncomingRequest(const proto::ReqMsg& req,
                                     proto::RespMsg* resp)
      ABSL_LOCKS_EXCLUDED(peer_hosts_mu_);

  // Handles out-of-band HostInfo exchange requests.
  absl::Status handleHostInfo(const proto::ReqMsg& req, proto::RespMsg* resp)
      ABSL_LOCKS_EXCLUDED(peer_hosts_mu_);

  // Retrieves an active client stub for peer_addr or instantiates a new one.
  const GrpcClient& getOrCreateClient(const Endpoint& peer)
      ABSL_LOCKS_EXCLUDED(peer_clients_mu_);

  // Returns true iff the invariant holds.
  bool invariant() const {
    return self_.control_plane_listener.HasNonzeroIpPort() &&
           grpc_server_ != nullptr && client_creds_ != nullptr;
  }

 private:
  const HostInfo& self_;
  std::shared_ptr<grpc::ServerCredentials> server_creds_;
  std::unique_ptr<GrpcServer> grpc_server_;
  std::shared_ptr<grpc::ChannelCredentials> client_creds_;

  absl::Mutex peer_clients_mu_;
  absl::flat_hash_map<Endpoint, std::unique_ptr<GrpcClient>> peer_clients_
      ABSL_GUARDED_BY(peer_clients_mu_);

  absl::Mutex peer_hosts_mu_;
  absl::flat_hash_map<Endpoint, std::unique_ptr<HostInfo>> peer_hosts_
      ABSL_GUARDED_BY(peer_hosts_mu_);
};

}  // namespace peregrine::internal

#endif  // PEREGRINE_SRC_INTERNAL_CONTROL_CONTROL_H_
