#ifndef PEREGRINE_SRC_INTERNAL_SOCKET_IP_UTIL_H_
#define PEREGRINE_SRC_INTERNAL_SOCKET_IP_UTIL_H_

#include <netinet/in.h>

#include <string>
#include <string_view>

#include "absl/status/statusor.h"
#include "src/internal/base/types.h"

namespace peregrine {

// Parses the `ip` address and returns an `in_addr` struct if successful.
// Otherwise, returns an error status.
absl::StatusOr<ipv4_t> ParseIPv4Addr(std::string_view ip);

// Parses the `ip` address and returns an `in6_addr` struct if successful.
// Otherwise, returns an error status.
absl::StatusOr<ipv6_t> ParseIPv6Addr(std::string_view ip);

// Returns a "ipv4:port" or "[ipv6]:port" string.
std::string ToIpAddrPortString(const struct sockaddr_storage& ss);

// Parses the `ip` address and `port` and returns a `sockaddr_in` struct.
// REQUIRE: `ip` is an IPv4 address.
struct sockaddr_in BuildIPv4Sockaddr(const IpAddr& ip, port_t port);

// Parses the `ip` address and `port` and returns a `sockaddr_in6` struct.
// REQUIRE: `ip` is an IPv6 address.
struct sockaddr_in6 BuildIPv6Sockaddr(const IpAddr& ip, port_t port);

}  // namespace peregrine

#endif  // PEREGRINE_SRC_INTERNAL_SOCKET_IP_UTIL_H_
