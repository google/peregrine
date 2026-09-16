#ifndef PEREGRINE_SRC_INTERNAL_BASE_HOSTINFO_H_
#define PEREGRINE_SRC_INTERNAL_BASE_HOSTINFO_H_

#include <ostream>
#include <string>
#include <string_view>
#include <vector>

#include "src/internal/base/endpoint.h"
#include "src/internal/base/nicinfo.h"

namespace peregrine::internal {

// This struct represents the host information.
// It is thread-compatible and but not thread-safe.
struct HostInfo {
  Endpoint control_plane_listener;
  std::vector<NicInfo> data_plane_listeners;

  // Parses and creates host info from a string.
  // Returns invalid host info if the string parsing fails.
  static HostInfo Create(std::string_view s);

  // Returns true iff all endpoints and RDMA interfaces are valid.
  bool IsValid() const;

  // Returns a string representation of the host info.
  std::string ToString() const;

  bool operator==(const HostInfo& other) const = default;
};

inline std::ostream& operator<<(std::ostream& os, const HostInfo& h) {
  return os << h.ToString();
}

}  // namespace peregrine::internal

#endif  // PEREGRINE_SRC_INTERNAL_BASE_HOSTINFO_H_
