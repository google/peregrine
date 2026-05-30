#ifndef PEREGRINE_SRC_INTERNAL_TRANSPORT_IMPL_H_
#define PEREGRINE_SRC_INTERNAL_TRANSPORT_IMPL_H_

#include <string_view>

#include "absl/base/optimization.h"
#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/strings/str_cat.h"
#include "src/api/transport.h"
#include "src/api/types.h"
#include "src/internal/base/endpoint.h"
#include "src/internal/engine/engine.h"

namespace peregrine::internal {

// This lightweight class implements the transport API. It delegates all the
// heavy work to its `engine`. In other words, this class serves as a thin
// adapter between the transport API and the engine, so the latter is has
// more flexibility to change.
// It is thread-safe.
class TransportImpl final : public Transport {
 public:
  // Posts a transport `request` to communicate with the `peer`.
  //
  // If successful, returns a `handle` which uniquely identifies the request
  // within this process. Otherwise, returns an error status. Once the `handle`
  // is returned, it is the caller's responsibility to keep the local/remote
  // memories specified by the `request` valid until it is completely served.
  absl::StatusOr<Handle> Post(std::string_view peer,
                              const Request& request) override {
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

  // Polls the status of the transport request identified by the `handle`.
  //
  // If the `handle` doesn't exist, returns an error status. Otherwise, returns
  // its status and, if the request is completely served, removes the `handle`.
  absl::StatusOr<Status> Poll(Handle handle) override {
    return engine_.QueryUpdate(handle);
  }

 private:
  Engine engine_;
};

}  // namespace peregrine::internal

#endif  // PEREGRINE_SRC_INTERNAL_TRANSPORT_IMPL_H_
