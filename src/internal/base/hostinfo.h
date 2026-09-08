#ifndef PEREGRINE_SRC_INTERNAL_BASE_HOSTINFO_H_
#define PEREGRINE_SRC_INTERNAL_BASE_HOSTINFO_H_

#include <cstdint>
#include <ostream>
#include <string>
#include <string_view>
#include <vector>

#include "absl/strings/str_cat.h"
#include "src/internal/base/endpoint.h"

namespace peregrine::internal {

// This struct represents a RDMA interface.
struct RdmaNic {
  std::string name;
  std::string gid;  // 16-byte
  uint32_t port = 1;

  // Returns true iff the interface is valid.
  bool IsValid() const { return !name.empty() && gid.size() == 16 && port > 0; }

  // Returns a string representation of the interface.
  std::string ToString() const { return absl::StrCat(name, "-port:", port); }

  bool operator==(const RdmaNic& other) const = default;
};

inline std::ostream& operator<<(std::ostream& os, const RdmaNic& r) {
  return os << r.ToString();
}

// This struct represents the host information.
// It is thread-compatible and but not thread-safe.
//
// TODO: Consider unifying TCP data_plane_listeners and rdma_interfaces into a
// polymorphic DataPlaneEndpoint variant.
struct HostInfo {
  Endpoint control_plane_listener;
  std::vector<Endpoint> data_plane_listeners;
  std::vector<RdmaNic> rdma_nics;

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
