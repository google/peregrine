#ifndef PEREGRINE_SRC_INTERNAL_TRANSPORT_IMPL_H_
#define PEREGRINE_SRC_INTERNAL_TRANSPORT_IMPL_H_

#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/strings/str_format.h"
#include "src/api/transport.h"
#include "src/api/types.h"
#include "src/internal/engine/engine.h"

namespace peregrine {

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
  // memories pointed by the `request.{laddr,raddr}` valid until the request
  // is completely served.
  absl::StatusOr<Handle> Post(Endpoint peer, const Request& request) override {
    if (!request.IsValid()) {
      return absl::InvalidArgumentError(
          absl::StrFormat("Invalid %s", request.ToString()));
    }
    return engine_.Enqueue(peer, request);
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

}  // namespace peregrine

#endif  // PEREGRINE_SRC_INTERNAL_TRANSPORT_IMPL_H_
