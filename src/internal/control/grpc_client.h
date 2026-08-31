#ifndef PEREGRINE_SRC_INTERNAL_CONTROL_GRPC_CLIENT_H_
#define PEREGRINE_SRC_INTERNAL_CONTROL_GRPC_CLIENT_H_

#include <memory>

#include "absl/status/statusor.h"
#include "grpcpp/security/credentials.h"
#include "src/internal/base/endpoint.h"
#include "src/internal/control/message.pb.h"
#include "src/internal/control/service.grpc.pb.h"
#include "src/util/macro.h"

namespace peregrine::internal {

// gRPC client wrapper for sending control requests to a peer gRPC server.
// It is thread-safe.
class GrpcClient final {
 public:
  // Constructor binding to a peer endpoint (e.g. "127.0.0.1:50051")
  // using explicit gRPC channel credentials.
  GrpcClient(const Endpoint& peer,
             std::shared_ptr<grpc::ChannelCredentials> creds);

  // Disallows copy/move operations.
  DISALLOW_COPY(GrpcClient);
  DISALLOW_MOVE(GrpcClient);

  // Destructor.
  ~GrpcClient() = default;

  // Synchronously sends a request to the peer and awaits a response.
  // Returns the response on success, or an error status on failure.
  absl::StatusOr<proto::RespMsg> SendUnary(const proto::ReqMsg& request) const;

 private:
  // Returns true if the invariant holds.
  bool invariant() const { return stub_ != nullptr; }

 private:
  std::unique_ptr<control::PeregrineService::Stub> stub_;
};

}  // namespace peregrine::internal

#endif  // PEREGRINE_SRC_INTERNAL_CONTROL_GRPC_CLIENT_H_
