#include "src/internal/control/grpc_client.h"

#include <memory>
#include <utility>

#include "absl/base/optimization.h"
#include "absl/log/check.h"
#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/strings/str_format.h"
#include "grpcpp/client_context.h"
#include "grpcpp/create_channel.h"
#include "grpcpp/security/credentials.h"
#include "grpcpp/support/status.h"
#include "src/internal/base/endpoint.h"
#include "src/internal/control/message.pb.h"
#include "src/internal/control/service.grpc.pb.h"

namespace peregrine::internal {

namespace {
absl::Status ToAbslStatus(const grpc::Status& s) {
  return absl::Status(static_cast<absl::StatusCode>(s.error_code()),
                      absl::StrFormat("gRPC call failed: %s (%s)",
                                      s.error_message(), s.error_details()));
}
}  // namespace

GrpcClient::GrpcClient(const Endpoint& peer,
                       std::shared_ptr<grpc::ChannelCredentials> creds)
    : stub_(control::PeregrineService::NewStub(
          grpc::CreateChannel(peer.ToString(), std::move(creds)))) {
  DCHECK(invariant());
}

absl::StatusOr<proto::RespMsg> GrpcClient::SendUnary(
    const proto::ReqMsg& request) const {
  DCHECK(invariant());

  grpc::ClientContext context;
  proto::RespMsg response;
  const grpc::Status s = stub_->ProcessUnary(&context, request, &response);
  if ABSL_PREDICT_FALSE (!s.ok()) return ToAbslStatus(s);
  return response;
}

absl::StatusOr<proto::PspKeyExchangeResponse> GrpcClient::ExchangePspKey(
    const proto::PspKeyExchangeRequest& request) const {
  DCHECK(invariant());

  grpc::ClientContext context;
  proto::PspKeyExchangeResponse response;
  const grpc::Status s = stub_->ExchangePspKey(&context, request, &response);
  if ABSL_PREDICT_FALSE (!s.ok()) return ToAbslStatus(s);
  return response;
}

}  // namespace peregrine::internal
