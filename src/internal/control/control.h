#ifndef PEREGRINE_SRC_INTERNAL_CONTROL_CONTROL_H_
#define PEREGRINE_SRC_INTERNAL_CONTROL_CONTROL_H_

#include <cstdint>
#include <memory>
#include <string_view>
#include <utility>

#include "absl/base/thread_annotations.h"
#include "absl/container/flat_hash_map.h"
#include "absl/functional/any_invocable.h"
#include "absl/log/check.h"
#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/synchronization/mutex.h"
#include "absl/types/span.h"
#include "grpcpp/security/credentials.h"
#include "grpcpp/security/server_credentials.h"
#include "src/internal/base/config.h"
#include "src/internal/base/endpoint.h"
#include "src/internal/base/hostinfo.h"
#include "src/internal/control/grpc_client.h"
#include "src/internal/control/grpc_server.h"
#include "src/internal/control/message.pb.h"
#include "src/internal/control/message_internal.pb.h"
#include "src/internal/socket/psp/tcp_psp_helper.h"
#include "src/util/macro.h"

namespace peregrine::internal {

// This class implements the transport control plane for endpoints to exchange
// request and response control messages.
// It is thread-safe.
class Control final {
 public:
  using PspKeyHandler = absl::AnyInvocable<absl::Status(
      const proto::PspKeyRequest&, proto::PspKeyResponse*) const>;
  using RdmaConnectHandler = absl::AnyInvocable<absl::Status(
      const proto::RdmaConnectRequest&, proto::RdmaConnectResponse*) const>;

  // Creates a control plane instance.
  static std::unique_ptr<Control> Create(const Config& config,
                                         const HostInfo& self,
                                         SecurityCredentials creds);

  // Disallows copy and move.
  DISALLOW_COPY(Control);
  DISALLOW_MOVE(Control);

  // Destructor.
  ~Control();

  // Starts the gRPC server to begin accepting incoming request messages.
  // Must be called only after host info is ready to be served.
  bool Start();

  // Registers a callback handler for incoming PSP key exchange requests.
  void SetPspKeyHandler(PspKeyHandler handler);

  // Registers a callback handler for incoming RDMA connect requests.
  void SetRdmaConnectHandler(RdmaConnectHandler handler);

  // Synchronously sends a request message to a remote peer endpoint.
  // Returns the response message or an error status.
  absl::StatusOr<proto::RespMsg> SendRequest(const Endpoint& peer,
                                             const proto::ReqMsg& req);

  // Returns the HostInfo of a remote peer.
  absl::StatusOr<HostInfo> GetPeerHostInfo(const Endpoint& peer)
      ABSL_LOCKS_EXCLUDED(peer_hosts_mu_);

  // Exchanges PSP encryption keys out-of-band with a remote peer.
  absl::StatusOr<PspSpiKey> ExchangePspKey(const Endpoint& peer,
                                           const PspSpiKey& psp,
                                           const Endpoint& target);

  // Exchanges QP credentials out-of-band with a remote peer.
  absl::StatusOr<proto::RdmaConnectResponse> ConnectRdmaPeer(
      const Endpoint& peer, std::string_view device_name, uint32_t qpn,
      absl::Span<const uint8_t> gid, uint32_t psn = 0, uint32_t rkey = 0);

 private:
  // Constructor.
  Control(const Config& config, const HostInfo& self, SecurityCredentials creds)
      : config_(config),
        self_(self),
        server_creds_(std::move(creds.server_creds)),
        client_creds_(std::move(creds.client_creds)) {
    DCHECK(config_.IsValid());
    DCHECK(self_.control_plane_listener.HasNonzeroIpPort());
    DCHECK_NE(server_creds_, nullptr);
    DCHECK_NE(client_creds_, nullptr);
  }

  // Returns true iff the invariant holds.
  bool invariant() const {
    return config_.IsValid() &&
           self_.control_plane_listener.HasNonzeroIpPort() &&
           client_creds_ != nullptr && grpc_server_ != nullptr;
  }

 private:
  // Retrieves an active client stub for peer_addr or instantiates a new one.
  const GrpcClient& getOrCreateClient(const Endpoint& peer)
      ABSL_LOCKS_EXCLUDED(peer_clients_mu_);

 private:
  // Handles incoming RPC requests by dispatching to dedicated message handlers.
  absl::Status handleIncomingRequest(const proto::ReqMsg& req,
                                     proto::RespMsg* resp);

  // Handles out-of-band HostInfo exchange requests.
  absl::Status handleHostInfo(const proto::ReqMsg& req, proto::RespMsg* resp)
      ABSL_LOCKS_EXCLUDED(peer_hosts_mu_);

  // Handles incoming PSP key exchange requests.
  absl::Status handlePspKeyExchange(const proto::ReqMsg& req,
                                    proto::RespMsg* resp)
      ABSL_LOCKS_EXCLUDED(psp_key_handler_mu_);

  // Handles incoming RDMA connection requests.
  absl::Status handleRdmaConnect(const proto::ReqMsg& req, proto::RespMsg* resp)
      ABSL_LOCKS_EXCLUDED(rdma_handler_mu_);

 private:
  const Config& config_;
  const HostInfo& self_;
  std::shared_ptr<grpc::ServerCredentials> server_creds_;
  std::shared_ptr<grpc::ChannelCredentials> client_creds_;
  std::unique_ptr<GrpcServer> grpc_server_;

  absl::Mutex peer_clients_mu_;
  absl::flat_hash_map<Endpoint, std::unique_ptr<GrpcClient>> peer_clients_
      ABSL_GUARDED_BY(peer_clients_mu_);

  absl::Mutex peer_hosts_mu_;
  absl::flat_hash_map<Endpoint, HostInfo> peer_hosts_
      ABSL_GUARDED_BY(peer_hosts_mu_);

  absl::Mutex psp_key_handler_mu_;
  PspKeyHandler psp_key_handler_ ABSL_GUARDED_BY(psp_key_handler_mu_);

  absl::Mutex rdma_handler_mu_;
  RdmaConnectHandler rdma_connect_handler_ ABSL_GUARDED_BY(rdma_handler_mu_);
};

}  // namespace peregrine::internal

#endif  // PEREGRINE_SRC_INTERNAL_CONTROL_CONTROL_H_
