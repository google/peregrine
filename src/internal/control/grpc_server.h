#ifndef PEREGRINE_SRC_INTERNAL_CONTROL_GRPC_SERVER_H_
#define PEREGRINE_SRC_INTERNAL_CONTROL_GRPC_SERVER_H_

#include <memory>
#include <utility>

#include "absl/functional/any_invocable.h"
#include "absl/log/check.h"
#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "grpcpp/security/server_credentials.h"
#include "grpcpp/server.h"
#include "grpcpp/server_context.h"
#include "grpcpp/support/status.h"
#include "src/internal/assumptions.h"
#include "src/internal/base/endpoint.h"
#include "src/internal/control/message.pb.h"
#include "src/internal/control/service.grpc.pb.h"
#include "src/internal/control/service.pb.h"
#include "src/util/macro.h"

namespace peregrine::internal {

// gRPC server listening for peer requests and executing callback handlers.
// It is thread-safe.
class GrpcServer final : public control::PeregrineService::Service {
  static_assert(assumptions::kThereIsOnlyOnePairOfWrapperControlMessages);

 public:
  using RequestHandler = absl::AnyInvocable<absl::Status(
      const proto::ReqMsg&, proto::RespMsg*) const>;

  // Creates an asynchronous gRPC server using explicit server credentials and
  // request handler.
  static absl::StatusOr<std::unique_ptr<GrpcServer>> Create(
      const Endpoint& self, std::shared_ptr<grpc::ServerCredentials> creds,
      RequestHandler&& handler);

  // Disallows copy/move operations.
  DISALLOW_COPY(GrpcServer);
  DISALLOW_MOVE(GrpcServer);

  // Destructor.
  ~GrpcServer() override { Shutdown(); }

  // Returns the actual TCP port bound by the gRPC server.
  int Port() const { return port_; }

  // Processes a unary RPC request.
  // Returns OK on success, or an error status on failure.
  grpc::Status ProcessUnary(grpc::ServerContext* context,
                            const proto::ReqMsg* request,
                            proto::RespMsg* response) override;

  // Gracefully terminates the gRPC server.
  void Shutdown();

 private:
  // Constructor.
  explicit GrpcServer(RequestHandler&& handler)
      : port_(0), server_(nullptr), handler_(std::move(handler)) {
    DCHECK_NE(handler_, nullptr);
  }

  // Returns true if the gRPC server is in a valid state.
  bool invariant() const {
    return port_ > 0 && server_ != nullptr && handler_ != nullptr;
  }

 private:
  int port_;
  std::unique_ptr<grpc::Server> server_;
  const RequestHandler handler_;
};

}  // namespace peregrine::internal

#endif  // PEREGRINE_SRC_INTERNAL_CONTROL_GRPC_SERVER_H_
