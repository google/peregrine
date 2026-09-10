#ifndef PEREGRINE_SRC_API_TRANSPORT_H_
#define PEREGRINE_SRC_API_TRANSPORT_H_

#include <cstddef>
#include <string_view>

#include "absl/functional/any_invocable.h"
#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/types/span.h"
#include "src/api/transport_types.h"

namespace peregrine {

// This class defines an asynchronous transport interface.
class Transport {
 public:
  // Destructor.
  virtual ~Transport() = default;

  // Posts a batch of transport `requests` to communicate with the `peer`.
  //
  // On success, returns a process-level unique `handle`, which can be used to
  // poll the request batch status. Returns an error on failure.
  //
  // If `on_complete` is provided, it is invoked once when the batch of requests
  // completes (either successfully or with an error).
  //
  // The caller must maintain the validity of the local/remote memory buffers
  // specified by the `request` until processing is complete.
  virtual absl::StatusOr<Handle> Post(
      std::string_view peer, absl::Span<const Request> requests,
      absl::AnyInvocable<void(Status)> on_complete = nullptr) = 0;

  // Polls the status of the transport request identified by the `handle`.
  //
  // If the `handle` is not found, returns an error. Otherwise, returns the
  // request status and automatically removes the `handle` if processing is
  // complete.
  virtual absl::StatusOr<Status> Poll(Handle handle) = 0;

  // Registers a contiguous memory buffer of `length` bytes starting at `addr`
  // with the transport.
  //
  // Pins pages and registers the memory across all active hardware adapters.
  // Returns an error status if registration fails.
  virtual absl::Status RegisterMemory(void* addr, size_t length) = 0;

  // Unregisters the memory buffer starting at base `addr` previously registered
  // via RegisterMemory().
  //
  // Unpins pages and releases hardware memory regions. Returns an error status
  // if the action fails.
  virtual absl::Status UnregisterMemory(const void* addr) = 0;

  // Returns a snapshot of the transport metrics.
  virtual TransportMetrics GetTransportMetrics() const = 0;
};

}  // namespace peregrine

#endif  // PEREGRINE_SRC_API_TRANSPORT_H_
