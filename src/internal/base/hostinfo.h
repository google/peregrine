#ifndef PEREGRINE_SRC_INTERNAL_BASE_HOSTINFO_H_
#define PEREGRINE_SRC_INTERNAL_BASE_HOSTINFO_H_

#include <cstdint>
#include <ostream>
#include <string>
#include <string_view>
#include <vector>

#include "src/internal/base/endpoint.h"

namespace peregrine::internal {

// Represents a local RDMA hardware interface on the host.
struct RdmaInterface {
  std::string name;
  std::string gid;  // 16-byte raw GID
  uint32_t port_num = 1;

  bool IsValid() const {
    return !name.empty() && gid.size() == 16 && port_num > 0;
  }

  bool operator==(const RdmaInterface& other) const = default;
};

// This struct represents the host information.
// It is thread-compatible and but not thread-safe.
//
// TODO: Consider unifying TCP data_plane_listeners and rdma_interfaces into a
// polymorphic DataPlaneEndpoint variant.
struct HostInfo {
  Endpoint control_plane_listener;
  std::vector<Endpoint> data_plane_listeners;
  std::vector<RdmaInterface> rdma_interfaces;

  // Parses and creates host info from a string, e.g.,
  // "10.0.0.1:10000, 10.0.0.1:35247, 10.0.0.2:51691".
  // Returns invalid host info if the string parsing fails.
  static HostInfo Create(std::string_view ipaddr_port_pairs);

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
