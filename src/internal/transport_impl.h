#ifndef PEREGRINE_SRC_INTERNAL_TRANSPORT_IMPL_H_
#define PEREGRINE_SRC_INTERNAL_TRANSPORT_IMPL_H_

#include <memory>
#include <string_view>
#include <utility>

#include "absl/log/check.h"
#include "absl/status/statusor.h"
#include "src/api/transport.h"
#include "src/api/types.h"
#include "src/internal/engine/engine.h"
#include "src/internal/socket/acceptor.h"

namespace peregrine::internal {

// This lightweight class implements the transport API. It delegates all the
// heavy work to its `engine`. In other words, this class serves as a thin
// adapter between the transport API and the engine, so the latter has more
// flexibility to change.
// It is thread-safe.
class TransportImpl final : public Transport {
 public:
  // Constructor.
  explicit TransportImpl(std::unique_ptr<TcpAcceptor> acceptor)
      : engine_(std::move(acceptor)) {}

  // Posts a transport `request` to communicate with the `peer`.
  //
  // If successful, returns a `handle` which uniquely identifies the request
  // within this process. Otherwise, returns an error status. Once the `handle`
  // is returned, it is the caller's responsibility to keep the local/remote
  // memories specified by the `request` valid until it is completely served.
  absl::StatusOr<Handle> Post(std::string_view peer,
                              const Request& request) override;

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
