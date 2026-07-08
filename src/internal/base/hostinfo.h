#ifndef PEREGRINE_SRC_INTERNAL_BASE_HOSTINFO_H_
#define PEREGRINE_SRC_INTERNAL_BASE_HOSTINFO_H_

#include <vector>

#include "src/internal/base/endpoint.h"

namespace peregrine::internal {

// This struct represents the host information.
// It is thread-compatible and but not thread-safe.
struct HostInfo {
  Endpoint control_plane_listener;
  std::vector<Endpoint> data_plane_listeners;

  // Returns true iff all endpoints are valid.
  bool IsValid() const;
};

}  // namespace peregrine::internal

#endif  // PEREGRINE_SRC_INTERNAL_BASE_HOSTINFO_H_
