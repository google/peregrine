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
#include "src/api/transport_types.h"
#include "src/internal/base/config.h"
#include "src/internal/base/endpoint.h"
#include "src/internal/base/hostinfo.h"
#include "src/internal/control/grpc_client.h"
#include "src/internal/control/grpc_server.h"
#include "src/internal/control/message.pb.h"
#include "src/internal/control/message_internal.pb.h"
#include "src/internal/metrics/control_metrics.h"
#include "src/internal/socket/psp/psp.h"
#include "src/util/macro.h"

namespace peregrine::internal {

// This class implements the transport control plane for endpoints to exchange
// request and response control messages.
// It is thread-safe.
class Control final {
 public:
  using PspTcpHandler = absl::AnyInvocable<absl::StatusOr<PspToken>(
      const PspToken& peer_token, const Endpoint& self_target) const>;
  using RdmaConnHandler = absl::AnyInvocable<absl::Status(
      const proto::RdmaConnReq&, proto::RdmaConnResp*) const>;

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
  // Must be called only after the `self_` host info is ready to be served.
  // Returns true if the server was started successfully, false otherwise.
  bool Start();

  // Sets the callback handler for incoming psp token exchange requests.
  void SetPspTcpHandler(PspTcpHandler handler) {
    absl::MutexLock _(psp_handler_mu_);
    psp_tcp_handler_ = std::move(handler);
  }

  // Sets the callback handler for incoming rdma connect requests.
  void SetRdmaConnHandler(RdmaConnHandler handler) {
    absl::MutexLock _(rdma_handler_mu_);
    rdma_conn_handler_ = std::move(handler);
  }

  // Returns the host info of the `peer` endpoint.
  absl::StatusOr<HostInfo> GetPeerHostInfo(const Endpoint& peer)
      ABSL_LOCKS_EXCLUDED(peer_hosts_mu_);

  // Exchanges psp tokens with the `peer` endpoint.
  absl::StatusOr<PspToken> ExchangePspTokens(const PspToken& self_token,
                                             const Endpoint& peer_target,
                                             const Endpoint& peer);

  // Exchanges rdma connection parameters with the `peer` endpoint.
  // TODO(mubashirq): return an internal struct instead of proto.
  absl::StatusOr<proto::RdmaConnResp> ConnectRdmaPeer(
      const Endpoint& peer, std::string_view device_name, uint32_t qpn,
      absl::Span<const uint8_t> gid, uint32_t psn = 0, uint32_t rkey = 0);

  // Takes a snapshot of control metrics.
  void GetMetricsSnapshot(TransportMetrics& m) const { metrics_.Snapshot(m); }

 private:
  // Constructor.
  Control(const Config& config, const HostInfo& self, SecurityCredentials creds)
      : config_(config),
        self_(self),
        server_creds_(creds.server_creds),
        client_creds_(creds.client_creds),
        psp_tcp_handler_(nullptr),
        rdma_conn_handler_(nullptr) {
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
  // Retrieves an active client stub for `peer` or instantiates a new one.
  const GrpcClient& getOrCreateClient(const Endpoint& peer)
      ABSL_LOCKS_EXCLUDED(peer_clients_mu_);

  // Synchronously sends a request message to the `peer` endpoint.
  // Returns the response message or an error status.
  absl::StatusOr<proto::RespMsg> sendRequest(const Endpoint& peer,
                                             const proto::ReqMsg& req_msg);

 private:
  // Handles all types of incoming rpc requests.
  absl::Status handleAllRequest(const proto::ReqMsg& req_msg,
                                proto::RespMsg* resp_msg);

  // Handles host info exchange.
  absl::Status handleHostInfoExchange(const proto::HostInfo& req_proto,
                                      proto::HostInfo* resp_proto)
      ABSL_LOCKS_EXCLUDED(peer_hosts_mu_);

  // Handles psp token exchange.
  absl::Status handlePspTokenExchange(const proto::PspTcpReq& req_proto,
                                      proto::PspTcpResp* resp_proto)
      ABSL_LOCKS_EXCLUDED(psp_handler_mu_);

  // Handles rdma connection establishment.
  absl::Status handleRdmaConnect(const proto::RdmaConnReq& req_proto,
                                 proto::RdmaConnResp* resp_proto)
      ABSL_LOCKS_EXCLUDED(rdma_handler_mu_);

 private:
  const Config& config_;
  const HostInfo& self_;

  std::shared_ptr<grpc::ServerCredentials> server_creds_;
  std::shared_ptr<grpc::ChannelCredentials> client_creds_;
  std::unique_ptr<GrpcServer> grpc_server_;

  ControlMetrics metrics_;

  absl::Mutex peer_clients_mu_;
  absl::flat_hash_map<Endpoint, std::unique_ptr<GrpcClient>> peer_clients_
      ABSL_GUARDED_BY(peer_clients_mu_);

  absl::Mutex peer_hosts_mu_;
  absl::flat_hash_map<Endpoint, HostInfo> peer_hosts_
      ABSL_GUARDED_BY(peer_hosts_mu_);

  absl::Mutex psp_handler_mu_;
  PspTcpHandler psp_tcp_handler_ ABSL_GUARDED_BY(psp_handler_mu_);

  absl::Mutex rdma_handler_mu_;
  RdmaConnHandler rdma_conn_handler_ ABSL_GUARDED_BY(rdma_handler_mu_);
};

}  // namespace peregrine::internal

#endif  // PEREGRINE_SRC_INTERNAL_CONTROL_CONTROL_H_
