#include "src/internal/transport_impl.h"

#include <string_view>

#include "absl/base/optimization.h"
#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/strings/str_cat.h"
#include "absl/types/span.h"
#include "src/api/transport_types.h"
#include "src/internal/base/endpoint.h"
#include "src/internal/engine/engine.h"

namespace peregrine::internal {

absl::StatusOr<Handle> TransportImpl::Post(std::string_view peer,
                                           absl::Span<const Request> requests) {
  const Endpoint endpoint = Endpoint::Create(peer);
  if ABSL_PREDICT_FALSE (!endpoint.IsValid()) {
    return absl::InvalidArgumentError(
        absl::StrCat("Invalid peer endpoint ", peer));
  }
  for (const auto& request : requests) {
    if ABSL_PREDICT_FALSE (!request.IsValid()) {
      return absl::InvalidArgumentError(
          absl::StrCat("Invalid ", request.ToString()));
    }
  }
  if (requests.size() != 1) {
    return absl::InvalidArgumentError(absl::StrCat(
        "Only single request is supported, got ", requests.size()));
  }
  return engine_.Enqueue(endpoint, requests[0]);
}

}  // namespace peregrine::internal
