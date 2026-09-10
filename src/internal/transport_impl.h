#ifndef PEREGRINE_SRC_INTERNAL_TRANSPORT_IMPL_H_
#define PEREGRINE_SRC_INTERNAL_TRANSPORT_IMPL_H_

#include <cstddef>
#include <memory>
#include <string_view>

#include "absl/functional/any_invocable.h"
#include "absl/log/check.h"
#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/types/span.h"
#include "src/api/transport.h"
#include "src/api/transport_types.h"
#include "src/internal/base/config.h"
#include "src/internal/base/endpoint.h"
#include "src/internal/base/hostinfo.h"
#include "src/internal/control/control.h"
#include "src/internal/engine/engine.h"

namespace peregrine::internal {

// This lightweight class implements the transport API. It delegates all the
// heavy work to its `engine`. In other words, this class serves as a thin
// adapter between the transport API and the engine, so the latter has more
// freedom to change.
// It is thread-safe.
class TransportImpl final : public Transport {
 public:
  // Creates a transport with the given `config` and control `endpoint`.
  static std::unique_ptr<TransportImpl> Create(const Config& config,
                                               const Endpoint& endpoint,
                                               SecurityCredentials creds);

  // Posts a batch of transport `requests` to communicate with the `peer`.
  //
  // On success, returns a process-level unique `handle`, which can be used to
  // poll the request batch status. Returns an error on failure.
  //
  // If `on_complete` is provided, it is invoked once when the batch of requests
  // complete (either successfully or with an error).
  //
  // The caller must maintain the validity of the local/remote memory buffers
  // specified by the `request` until processing is complete.
  absl::StatusOr<Handle> Post(
      std::string_view peer, absl::Span<const Request> requests,
      absl::AnyInvocable<void(Status)> on_complete = nullptr) override;

  // Polls the status of the transport request identified by the `handle`.
  //
  // If the `handle` is not found, returns an error. Otherwise, returns the
  // request status and automatically removes the `handle` if processing is
  // complete.
  absl::StatusOr<Status> Poll(Handle handle) override {
    return engine_->QueryUpdate(handle);
  }

  // Registers a contiguous memory buffer of `length` bytes starting at `addr`
  // across active RDMA hardware adapters.
  absl::Status RegisterMemory(void* addr, size_t length) override {
    return engine_->RegisterMemory(addr, length);
  }

  // Unregisters the memory buffer starting at base `addr` across active RDMA
  // hardware adapters.
  absl::Status UnregisterMemory(const void* addr) override {
    return engine_->UnregisterMemory(addr);
  }

  // Returns a snapshot of metrics.
  TransportMetrics GetTransportMetrics() const override;

 private:
  // Constructor.
  explicit TransportImpl(const Config& config) : config_(config) {}

 private:
  const Config config_;
  HostInfo self_;
  std::unique_ptr<Control> control_;
  std::unique_ptr<Engine> engine_;
};

}  // namespace peregrine::internal

#endif  // PEREGRINE_SRC_INTERNAL_TRANSPORT_IMPL_H_
