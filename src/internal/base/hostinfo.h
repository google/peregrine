#ifndef PEREGRINE_SRC_INTERNAL_BASE_HOSTINFO_H_
#define PEREGRINE_SRC_INTERNAL_BASE_HOSTINFO_H_

#include <ostream>
#include <string>
#include <string_view>
#include <vector>

#include "src/internal/base/endpoint.h"

namespace peregrine::internal {

// This struct represents the host information.
// It is thread-compatible and but not thread-safe.
struct HostInfo {
  Endpoint control_plane_listener;
  std::vector<Endpoint> data_plane_listeners;

  // Parses and creates host info from a string, e.g.,
  // "10.0.0.1:10000, 10.0.0.1:35247, 10.0.0.2:51691".
  // Returns invalid host info if the string parsing fails.
  static HostInfo Create(std::string_view ipaddr_port_pairs);

  // Returns true iff all endpoints are valid.
  bool IsValid() const;

  // Returns a string representation of the host info.
  std::string ToString() const;
};

inline std::ostream& operator<<(std::ostream& os, const HostInfo& h) {
  return os << h.ToString();
}

}  // namespace peregrine::internal

#endif  // PEREGRINE_SRC_INTERNAL_BASE_HOSTINFO_H_
