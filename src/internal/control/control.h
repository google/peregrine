#ifndef PEREGRINE_SRC_INTERNAL_CONTROL_CONTROL_H_
#define PEREGRINE_SRC_INTERNAL_CONTROL_CONTROL_H_

#include <deque>
#include <memory>
#include <string>
#include <string_view>
#include <thread>  // NOLINT

#include "absl/base/thread_annotations.h"
#include "absl/container/flat_hash_map.h"
#include "absl/functional/any_invocable.h"
#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/synchronization/mutex.h"
#include "grpcpp/security/credentials.h"
#include "grpcpp/security/server_credentials.h"
#include "src/internal/channel/channel.h"
#include "src/internal/control/grpc_client.h"
#include "src/internal/control/grpc_server.h"
#include "src/internal/control/message.pb.h"
#include "src/util/macro.h"

namespace peregrine::internal {

// This class implements the transport control plane for endpoints to exchange
// control messages, such as request, host/device info, heartbeat, etc.
// It is thread-safe.
class Control final {
 public:
  using RequestHandler =
      absl::AnyInvocable<absl::Status(const proto::ReqMsg&, proto::RespMsg*)>;

  // Constructor for legacy TCP socket channel mode.
  explicit Control(std::unique_ptr<Channel> channel);

  // Factory method creating a gRPC control communicator with explicit security
  // credentials.
  static absl::StatusOr<std::unique_ptr<Control>> CreateGrpcControl(
      std::string_view listen_addr, RequestHandler&& handler,
      std::shared_ptr<grpc::ServerCredentials> server_creds,
      std::shared_ptr<grpc::ChannelCredentials> client_creds);

  // Disallows copy and move.
  DISALLOW_COPY(Control);
  DISALLOW_MOVE(Control);

  // Destructor.
  ~Control();

  // === gRPC Communicator API ===

  // Synchronously sends a gRPC control query to a remote peer endpoint and
  // returns the reply.
  absl::StatusOr<proto::RespMsg> SendRequest(std::string_view peer_addr,
                                             const proto::ReqMsg& req);

  // Returns the actual TCP port bound by the gRPC listener (0 if running in
  // legacy TCP mode).
  int port() const;

  // === Legacy TCP Asynchronous Streaming Queue API ===

  // Enqueues a control message for sending.
  // Returns true iff the message is enqueued successfully.
  bool EnqueueSend(const proto::ReqMsg& msg);

  // Dequeues a received control message.
  // Returns true iff a message is dequeued.
  bool DequeueRecv(proto::ReqMsg& msg);

 private:
  // Private constructor initializing in pure gRPC mode.
  Control(std::unique_ptr<GrpcServer> server,
          std::shared_ptr<grpc::ChannelCredentials> client_creds);

  // Retrieves an active client stub for peer_addr or instantiates a new one.
  GrpcClient* getOrCreateClient(std::string_view peer_addr)
      ABSL_EXCLUSIVE_LOCKS_REQUIRED(mu_);

  // Returns true iff there are pending messages to send or the destructor is
  // called.
  bool hasSendWork() const ABSL_EXCLUSIVE_LOCKS_REQUIRED(mu_);

  // Runs in a thread to send control messages.
  void sendLoop();

  // Runs in a thread to receive control messages.
  void recvLoop();

 private:
  // Sends a control message to the `channel_`.
  // Returns true iff the message is sent successfully.
  bool send(const proto::ReqMsg& msg);

  // Receives a control message from the `channel_`.
  // Returns true iff the message is received successfully.
  bool recv(proto::ReqMsg& msg);

 private:
  mutable absl::Mutex mu_;
  bool stopping_ ABSL_GUARDED_BY(mu_);
  std::deque<proto::ReqMsg> send_queue_ ABSL_GUARDED_BY(mu_);
  std::deque<proto::ReqMsg> recv_queue_ ABSL_GUARDED_BY(mu_);

  std::unique_ptr<Channel> channel_;
  std::jthread send_thread_;
  std::jthread recv_thread_;
  std::unique_ptr<GrpcServer> grpc_server_;
  absl::flat_hash_map<std::string, std::unique_ptr<GrpcClient>> peer_clients_
      ABSL_GUARDED_BY(mu_);
  std::shared_ptr<grpc::ChannelCredentials> client_creds_;
};

}  // namespace peregrine::internal

#endif  // PEREGRINE_SRC_INTERNAL_CONTROL_CONTROL_H_
