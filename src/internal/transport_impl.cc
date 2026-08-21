#include "src/internal/transport_impl.h"

#include <memory>
#include <string_view>

#include "absl/base/optimization.h"
#include "absl/container/flat_hash_set.h"
#include "absl/log/log.h"
#include "absl/memory/memory.h"
#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/strings/str_cat.h"
#include "absl/types/span.h"
#include "grpcpp/security/credentials.h"
#include "grpcpp/security/server_credentials.h"
#include "src/api/transport_types.h"
#include "src/internal/base/endpoint.h"
#include "src/internal/base/hostinfo.h"
#include "src/internal/control/control.h"
#include "src/internal/engine/engine.h"

namespace peregrine::internal {

std::unique_ptr<TransportImpl> TransportImpl::Create(const Endpoint& control_ep,
                                                     int num_conns_per_peer) {
  if ABSL_PREDICT_FALSE (!control_ep.HasNonzeroIpPort()) {
    LOG(WARNING) << "invalid control endpoint: " << control_ep;
    return nullptr;
  }

  auto transport = absl::WrapUnique(new TransportImpl());
  transport->self_.control_plane_listener = control_ep;

  // TODO: Currently, insecure credentials are used for control plane
  // communication. These credentials should be provided externally via
  // CreateTransport(), which will be interfaced in upcoming CLs.
  transport->control_ =
      Control::Create(transport->self_, grpc::InsecureServerCredentials(),
                      grpc::InsecureChannelCredentials());
  if (transport->control_ == nullptr) {
    LOG(WARNING) << "failed to create control: " << transport->self_;
    return nullptr;
  }

  transport->engine_ = Engine::Create(num_conns_per_peer, transport->self_,
                                      *transport->control_);
  if (transport->engine_ == nullptr) {
    LOG(WARNING) << "failed to create engine: " << transport->self_;
    return nullptr;
  }

  if (!transport->self_.IsValid()) {
    LOG(WARNING) << "failed to create transport: invalid HostInfo "
                 << transport->self_.ToString();
    return nullptr;
  }

  // Start serving control RPCs only after HostInfo is fully populated by
  // engine to prevent startup races and ensure peers resolve valid data plane
  // listeners.
  if (!transport->control_->Start()) {
    LOG(WARNING) << "failed to start control plane: " << transport->self_;
    return nullptr;
  }

  return transport;
}

absl::StatusOr<Handle> TransportImpl::Post(std::string_view peer,
                                           absl::Span<const Request> requests) {
  const Endpoint endpoint = Endpoint::Create(peer);
  if ABSL_PREDICT_FALSE (!endpoint.HasNonzeroIpPort()) {
    return absl::InvalidArgumentError(
        absl::StrCat("Invalid peer endpoint ", peer));
  }

  if (requests.empty()) {
    return absl::InvalidArgumentError("Empty requests");
  }

  absl::flat_hash_set<Op> ops;
  for (const auto& request : requests) {
    if ABSL_PREDICT_FALSE (!request.IsValid()) {
      return absl::InvalidArgumentError(
          absl::StrCat("Invalid ", request.ToString()));
    }
    ops.insert(request.op);
  }

  // TODO(yongx): support mixed op types.
  if (ops.size() > 1) {
    return absl::InvalidArgumentError(
        "All requests must have the same op type");
  }

  return engine_->Enqueue(endpoint, requests);
}

}  // namespace peregrine::internal
