#include "src/internal/control/rpc_client.h"

#include <memory>
#include <string>
#include <string_view>
#include <utility>

#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/strings/str_format.h"
#include "grpcpp/client_context.h"
#include "grpcpp/create_channel.h"
#include "grpcpp/security/credentials.h"
#include "grpcpp/support/status.h"
#include "src/internal/control/message.pb.h"
#include "src/internal/control/service.grpc.pb.h"

namespace peregrine::internal {

RpcClient::RpcClient(std::string_view target_address,
                     std::shared_ptr<grpc::ChannelCredentials> creds)
    : stub_(rpc::PeregrineService::NewStub(grpc::CreateChannel(
          std::string(target_address), std::move(creds)))) {}

absl::StatusOr<proto::RespMsg> RpcClient::SendUnary(
    const proto::ReqMsg& request) const {
  grpc::ClientContext context;
  proto::RespMsg response;
  const grpc::Status grpc_status =
      stub_->ProcessUnary(&context, request, &response);
  if (!grpc_status.ok()) {
    return absl::Status(static_cast<absl::StatusCode>(grpc_status.error_code()),
                        absl::StrFormat("gRPC call failed: %s (%s)",
                                        grpc_status.error_message(),
                                        grpc_status.error_details()));
  }
  return response;
}

}  // namespace peregrine::internal
