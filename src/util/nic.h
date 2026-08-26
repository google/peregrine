#ifndef PEREGRINE_SRC_UTIL_NIC_H_
#define PEREGRINE_SRC_UTIL_NIC_H_

#include <sys/socket.h>

#include <string>
#include <vector>

#include "absl/container/flat_hash_map.h"

namespace peregrine::util {

// Enumerates the network interface cards (NICs) on the local machine.
absl::flat_hash_map<std::string, std::vector<std::string>> EnumerateNics();

// Returns a map of `interface -> routable ip addresses` matching the address
// `family` (AF_UNSPEC for any, AF_INET for IPv4, AF_INET6 for IPv6).
absl::flat_hash_map<std::string, std::vector<std::string>> FindRoutableIpAddrs(
    int family = AF_UNSPEC);

}  // namespace peregrine::util

#endif  // PEREGRINE_SRC_UTIL_NIC_H_
