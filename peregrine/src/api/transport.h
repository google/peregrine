#ifndef PEREGRINE_SRC_API_TRANSPORT_H_
#define PEREGRINE_SRC_API_TRANSPORT_H_

#include <cstddef>
#include <string>
#include <string_view>
#include <vector>

#include "absl/functional/any_invocable.h"
#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/types/span.h"
#include "peregrine/src/api/transport_metrics.h"
#include "peregrine/src/api/transport_trace.h"
#include "peregrine/src/api/transport_types.h"

namespace peregrine {

// This class defines an asynchronous transport interface.
class Transport {
 public:
  // Destructor.
  virtual ~Transport() = default;

  // Returns the control-plane endpoint of this transport instance.
  virtual std::string GetControlEndpoint() const = 0;

  // Posts a batch of transport `requests` to communicate with the `peer`.
  //
  // On success, returns a process-level unique `handle`, which can be used to
  // poll the request batch status. Returns an error on failure.
  //
  // If a non-null `on_complete` is provided, it is invoked once when the batch
  // of requests completes (either successfully or with an error), and the
  // `handle` is automatically removed. Callers providing `on_complete` do not
  // need to call `Poll()`.
  //
  // The caller must maintain the validity of the local/remote memory buffers
  // specified by the `request` until processing is complete.
  virtual absl::StatusOr<Handle> Post(
      std::string_view peer, absl::Span<const Request> requests,
      absl::AnyInvocable<void(Status)> on_complete = nullptr,
      const Metadata& metadata = {}) = 0;

  // Polls the status of the transport request identified by the `handle`.
  //
  // If the `handle` is not found, returns an error. Otherwise, returns the
  // request status and automatically removes the `handle` if processing is
  // complete.
  //
  // Callers providing a non-null `on_complete` callback to `Post()` must not
  // call this `Poll()` function.
  virtual absl::StatusOr<Status> Poll(Handle handle) = 0;

  // Sets the callback invoked on the receiver when all requests in an incoming
  // `Post()` batch complete. If not set (or set to `nullptr`), no callback is
  // invoked on post receive completion.
  virtual void SetRecvPostCallback(RecvCallback callback) = 0;

  // Sets the callback invoked on the receiver when an individual incoming
  // request completes. If not set (or set to `nullptr`), no callback is
  // invoked on request receive completion.
  virtual void SetRecvReqCallback(RecvCallback callback) = 0;

  // Registers a contiguous memory buffer of `length` bytes at the base `addr`.
  //
  // Pins the memory buffer in physical address space for RDMA access.
  // Returns OK on success, or an error status otherwise.
  virtual absl::Status RegisterMemory(void* addr, size_t length) = 0;

  // Unregisters the previously registered memory buffer at the base `addr`.
  //
  // Unpins the memory buffer from physical address space.
  // Returns OK on success, or an error status otherwise.
  virtual absl::Status UnregisterMemory(const void* addr) = 0;

  // Returns a snapshot of transport metrics.
  virtual TransportMetrics GetTransportMetrics() const = 0;

  // Dynamically updates the trace configuration (e.g., enabling/disabling
  // tracing or changing the sampling interval).
  virtual void ConfigureTrace(const TraceConfig& config) = 0;

  // Clears all buffered Post() trace records.
  virtual void ClearTrace() = 0;

  // Returns a snapshot of assembled Post() trace records currently in the
  // trace buffer.
  virtual std::vector<PostTraceRecord> GetTraceSnapshot() const = 0;

  // Exports all currently buffered Post() trace records in Chrome Trace Event
  // JSON format (compatible with Perfetto / chrome://tracing).
  virtual std::string ExportChromeTraceJson() const = 0;
};

}  // namespace peregrine

#endif  // PEREGRINE_SRC_API_TRANSPORT_H_
