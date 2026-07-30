#ifndef PEREGRINE_SRC_INTERNAL_CONTROL_RPC_CLIENT_H_
#define PEREGRINE_SRC_INTERNAL_CONTROL_RPC_CLIENT_H_

#include <memory>
#include <string_view>

#include "absl/status/statusor.h"
#include "grpcpp/security/credentials.h"
#include "src/internal/control/message.pb.h"
#include "src/internal/control/service.grpc.pb.h"
#include "src/util/macro.h"

namespace peregrine::internal {

// Client wrapper for sending unary control requests to a peer RpcServer.
// It is thread-safe.
class RpcClient final {
 public:
  // Constructor binding to a remote peer endpoint (e.g. "127.0.0.1:50051")
  // using explicit gRPC channel credentials.
  RpcClient(std::string_view target_address,
            std::shared_ptr<grpc::ChannelCredentials> creds);

  DISALLOW_COPY(RpcClient);
  DISALLOW_MOVE(RpcClient);

  ~RpcClient() = default;

  // Synchronously sends a control query and awaits the structured response.
  absl::StatusOr<proto::RespMsg> SendUnary(const proto::ReqMsg& request) const;

 private:
  std::unique_ptr<rpc::PeregrineService::Stub> stub_;
};

}  // namespace peregrine::internal

#endif  // PEREGRINE_SRC_INTERNAL_CONTROL_RPC_CLIENT_H_
