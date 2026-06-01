#ifndef PEREGRINE_SRC_INTERNAL_SOCKET_IP_UTIL_H_
#define PEREGRINE_SRC_INTERNAL_SOCKET_IP_UTIL_H_

#include <netinet/in.h>

#include <string>

#include "src/internal/base/endpoint.h"

namespace peregrine::internal {

// Returns a "ipv4:port" or "[ipv6]:port" string.
std::string ToIpAddrPortString(const struct sockaddr_storage& ss);

// Builds a `sockaddr_in` struct for the given endpoint.
// REQUIRE: the endpoint has an ipv4 address.
struct sockaddr_in BuildIPv4Sockaddr(const Endpoint& e);

// Builds a `sockaddr_in6` struct for the given endpoint.
// REQUIRE: the endpoint has an ipv6 address.
struct sockaddr_in6 BuildIPv6Sockaddr(const Endpoint& e);

}  // namespace peregrine::internal

#endif  // PEREGRINE_SRC_INTERNAL_SOCKET_IP_UTIL_H_
