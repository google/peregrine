#ifndef PEREGRINE_SRC_INTERNAL_TRANSPORT_IMPL_H_
#define PEREGRINE_SRC_INTERNAL_TRANSPORT_IMPL_H_

#include <memory>
#include <string_view>
#include <utility>

#include "absl/log/check.h"
#include "absl/status/statusor.h"
#include "absl/types/span.h"
#include "src/api/transport.h"
#include "src/api/transport_types.h"
#include "src/internal/base/hostinfo.h"
#include "src/internal/engine/engine.h"
#include "src/internal/socket/acceptor.h"

namespace peregrine::internal {

// This lightweight class implements the transport API. It delegates all the
// heavy work to its `engine`. In other words, this class serves as a thin
// adapter between the transport API and the engine, so the latter has more
// freedom to change.
// It is thread-safe.
class TransportImpl final : public Transport {
 public:
  // Constructor.
  explicit TransportImpl(std::unique_ptr<TcpAcceptor> acceptor,
                         const HostInfo& self, int num_conns_per_peer)
      : engine_(std::move(acceptor), self, num_conns_per_peer) {}

  // Posts a batch of transport `requests` to communicate with the `peer`.
  //
  // On success, returns a process-level unique `handle`, which can be used to
  // poll the request batch status. Returns an error on failure.
  //
  // The caller must maintain the validity of the local/remote memory buffers
  // specified by the `request` until processing is complete.
  absl::StatusOr<Handle> Post(std::string_view peer,
                              absl::Span<const Request> requests) override;

  // Polls the status of the transport request identified by the `handle`.
  //
  // If the `handle` is not found, returns an error. Otherwise, returns the
  // request status and automatically removes the `handle` if processing is
  // complete.
  absl::StatusOr<Status> Poll(Handle handle) override {
    return engine_.QueryUpdate(handle);
  }

 private:
  Engine engine_;
};

}  // namespace peregrine::internal

#endif  // PEREGRINE_SRC_INTERNAL_TRANSPORT_IMPL_H_
