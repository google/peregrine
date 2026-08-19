#ifndef PEREGRINE_SRC_INTERNAL_CONTROL_GRPC_SERVER_H_
#define PEREGRINE_SRC_INTERNAL_CONTROL_GRPC_SERVER_H_

#include <atomic>
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
#include "src/internal/base/endpoint.h"
#include "src/internal/control/message.pb.h"
#include "src/internal/control/service.grpc.pb.h"
#include "src/util/macro.h"

namespace peregrine::internal {

// gRPC server listening for peer requests and executing callback handlers.
// It is thread-safe.
class GrpcServer final : public rpc::PeregrineService::Service {
 public:
  using RequestHandler =
      absl::AnyInvocable<absl::Status(const proto::ReqMsg&, proto::RespMsg*)>;

  // Creates an asynchronous gRPC server using explicit server credentials.
  // Note: The server is created without a request handler attached; callers
  // must attach a handler before incoming requests can be processed.
  static absl::StatusOr<std::unique_ptr<GrpcServer>> Create(
      const Endpoint& self, std::shared_ptr<grpc::ServerCredentials> creds);

  // Disallows copy/move operations.
  DISALLOW_COPY(GrpcServer);
  DISALLOW_MOVE(GrpcServer);

  // Destructor.
  ~GrpcServer() override { Shutdown(); }

  // Sets the request handler callback once.
  void SetRequestHandler(RequestHandler&& handler) {
    DCHECK(handler_.load(std::memory_order_relaxed) == nullptr);
    owned_handler_ = std::make_unique<RequestHandler>(std::move(handler));
    handler_.store(owned_handler_.get(), std::memory_order_release);
    DCHECK(invariant());
  }

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
  GrpcServer()
      : port_(0),
        server_(nullptr),
        owned_handler_(nullptr),
        handler_(nullptr) {}

  // Returns true if the gRPC server is in a valid state.
  bool invariant() const { return port_ > 0 && server_ != nullptr; }

 private:
  int port_;
  std::unique_ptr<grpc::Server> server_;

  // Atomic pointer is sufficient because handler transitions from nullptr to
  // invocable exactly once and is never modified afterwards.
  std::unique_ptr<RequestHandler> owned_handler_;
  std::atomic<RequestHandler*> handler_;
};

}  // namespace peregrine::internal

#endif  // PEREGRINE_SRC_INTERNAL_CONTROL_GRPC_SERVER_H_
