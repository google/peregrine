#ifndef PEREGRINE_SRC_UTIL_NIC_H_
#define PEREGRINE_SRC_UTIL_NIC_H_

#include <sys/socket.h>

#include <cstdint>
#include <ostream>
#include <string>
#include <vector>

#include "absl/container/flat_hash_map.h"
#include "src/util/ipaddr.h"

namespace peregrine::util {

enum class NicType : uint8_t {
  kIP = 0,
  kRDMA = 1,
};

// Returns a string representation of the nic type.
std::string ToString(NicType t);

inline std::ostream& operator<<(std::ostream& os, NicType t) {
  return os << ToString(t);
}

struct NicInfo final {
  NicType type;
  std::vector<IpAddr> addrs;
};

// Returns a string representation of the nic info.
std::string ToString(const NicInfo& ni);

inline std::ostream& operator<<(std::ostream& os, const NicInfo& ni) {
  return os << ToString(ni);
}

// Enumerates the network interface cards (NICs) on the local machine.
absl::flat_hash_map<std::string, std::vector<std::string>> EnumerateNics();

// Returns a map of `interface -> routable ip addresses` matching the address
// `family` (AF_UNSPEC for any, AF_INET for IPv4, AF_INET6 for IPv6).
absl::flat_hash_map<std::string, NicInfo> FindRoutableIpAddrs(
    int family = AF_UNSPEC);

}  // namespace peregrine::util

#endif  // PEREGRINE_SRC_UTIL_NIC_H_
