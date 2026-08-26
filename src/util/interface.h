#ifndef PEREGRINE_SRC_UTIL_INTERFACE_H_
#define PEREGRINE_SRC_UTIL_INTERFACE_H_

#include <sys/socket.h>

#include <string>
#include <vector>

namespace peregrine::util {

// Returns all routable Multi-NIC IP addresses matching the given address family
// (AF_UNSPEC for any, AF_INET for IPv4, AF_INET6 for IPv6).
std::vector<std::string> EnumerateIpInterfaces(int family = AF_UNSPEC);

}  // namespace peregrine::util

#endif  // PEREGRINE_SRC_UTIL_INTERFACE_H_
