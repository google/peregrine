#ifndef PEREGRINE_SRC_INTERNAL_CONTROL_RPC_SERVER_H_
#define PEREGRINE_SRC_INTERNAL_CONTROL_RPC_SERVER_H_

#include <memory>
#include <string_view>

#include "absl/functional/any_invocable.h"
#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "grpcpp/security/server_credentials.h"
#include "grpcpp/server.h"
#include "grpcpp/server_context.h"
#include "grpcpp/support/status.h"
#include "src/internal/control/message.pb.h"
#include "src/internal/control/service.grpc.pb.h"
#include "src/util/macro.h"

namespace peregrine::internal {

// Server class listening for peer queries and executing callback handlers.
// It is thread-safe.
class RpcServer final : public rpc::PeregrineService::Service {
 public:
  using RequestHandler =
      absl::AnyInvocable<absl::Status(const proto::ReqMsg&, proto::RespMsg*)>;

  // Factory method instantiating an asynchronous listening server on the
  // specified address using explicit gRPC server credentials.
  static absl::StatusOr<std::unique_ptr<RpcServer>> Create(
      std::string_view listen_address, RequestHandler handler,
      std::shared_ptr<grpc::ServerCredentials> creds);

  DISALLOW_COPY(RpcServer);
  DISALLOW_MOVE(RpcServer);

  ~RpcServer() override;

  // Implements PeregrineService::Service::ProcessUnary.
  grpc::Status ProcessUnary(grpc::ServerContext* context,
                            const proto::ReqMsg* request,
                            proto::RespMsg* response) override;

  // Gracefully terminates active server listeners.
  void Shutdown();

  // Returns the actual TCP port bound by the gRPC listener.
  int port() const { return port_; }

 private:
  explicit RpcServer(RequestHandler handler);

 private:
  RequestHandler handler_;
  std::unique_ptr<grpc::Server> server_;
  int port_ = 0;
};

}  // namespace peregrine::internal

#endif  // PEREGRINE_SRC_INTERNAL_CONTROL_RPC_SERVER_H_
