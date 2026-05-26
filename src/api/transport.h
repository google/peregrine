#ifndef PEREGRINE_SRC_API_TRANSPORT_H_
#define PEREGRINE_SRC_API_TRANSPORT_H_

#include "absl/status/statusor.h"
#include "src/api/types.h"

namespace peregrine {

// This class defines an asynchronous transport interface.
class Transport {
 public:
  // Destructor.
  virtual ~Transport() = default;

  // Posts a transport `request` to communicate with the `peer`.
  //
  // If successful, returns a `handle` which uniquely identifies the request
  // within this process. Otherwise, returns an error status. Once the `handle`
  // is returned, it is the caller's responsibility to keep the local/remote
  // memories specified by the `request` valid until it is completely served.
  virtual absl::StatusOr<Handle> Post(Endpoint peer,
                                      const Request& request) = 0;

  // Polls the status of the transport request identified by the `handle`.
  //
  // If the `handle` doesn't exist, returns an error status. Otherwise, returns
  // its status and, if the request is completely served, removes the `handle`.
  virtual absl::StatusOr<Status> Poll(Handle handle) = 0;
};

}  // namespace peregrine

#endif  // PEREGRINE_SRC_API_TRANSPORT_H_
