#ifndef PEREGRINE_SRC_API_TRANSPORT_H_
#define PEREGRINE_SRC_API_TRANSPORT_H_

#include <string_view>

#include "absl/status/statusor.h"
#include "absl/types/span.h"
#include "src/api/types.h"

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
  // The caller must maintain the validity of the local/remote memory buffers
  // specified by the `request` until processing is complete.
  virtual absl::StatusOr<Handle> Post(std::string_view peer,
                                      absl::Span<const Request> requests) = 0;

  // Polls the status of the transport request identified by the `handle`.
  //
  // If the `handle` is not found, returns an error. Otherwise, returns the
  // request status and automatically removes the `handle` if processing is
  // complete.
  virtual absl::StatusOr<Status> Poll(Handle handle) = 0;
};

}  // namespace peregrine

#endif  // PEREGRINE_SRC_API_TRANSPORT_H_
