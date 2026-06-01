#include "src/internal/transport_impl.h"

#include <string_view>

#include "absl/base/optimization.h"
#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/strings/str_cat.h"
#include "src/api/types.h"
#include "src/internal/base/endpoint.h"
#include "src/internal/engine/engine.h"

namespace peregrine::internal {

absl::StatusOr<Handle> TransportImpl::Post(std::string_view peer,
                                           const Request& request) {
  const Endpoint endpoint = Endpoint::Create(peer);
  if ABSL_PREDICT_FALSE (!endpoint.IsValid()) {
    return absl::InvalidArgumentError(
        absl::StrCat("Invalid peer endpoint ", peer));
  }
  if ABSL_PREDICT_FALSE (!request.IsValid()) {
    return absl::InvalidArgumentError(
        absl::StrCat("Invalid ", request.ToString()));
  }
  return engine_.Enqueue(endpoint, request);
}

}  // namespace peregrine::internal
