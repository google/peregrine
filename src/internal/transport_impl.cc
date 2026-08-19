#include "src/internal/transport_impl.h"

#include <memory>
#include <string_view>
#include <utility>

#include "absl/base/optimization.h"
#include "absl/container/flat_hash_set.h"
#include "absl/log/log.h"
#include "absl/memory/memory.h"
#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/strings/str_cat.h"
#include "absl/types/span.h"
#include "src/api/transport_types.h"
#include "src/internal/base/endpoint.h"
#include "src/internal/base/hostinfo.h"
#include "src/internal/engine/engine.h"

namespace peregrine::internal {

std::unique_ptr<TransportImpl> TransportImpl::Create(const HostInfo& self,
                                                     int num_conns_per_peer) {
  auto engine = Engine::Create(self, num_conns_per_peer);
  if (engine == nullptr) {
    LOG(WARNING) << "failed to create engine: " << self;
    return nullptr;
  }

  return absl::WrapUnique(new TransportImpl(std::move(engine)));
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
