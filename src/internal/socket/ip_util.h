#ifndef PEREGRINE_SRC_INTERNAL_SOCKET_IP_UTIL_H_
#define PEREGRINE_SRC_INTERNAL_SOCKET_IP_UTIL_H_

#include <netinet/in.h>

#include <string>

#include "src/internal/base/types.h"

namespace peregrine {

// Returns the address family (AF_INET/AF_INET6/AF_UNSPEC) of the `ip` address.
int AddressFamily(ipaddr_t ip);

// Returns true iff the given `ip` is a valid IPv4 address.
bool IsIPv4Addr(ipaddr_t ip);

// Returns true iff the given `ip` is a valid IPv6 address.
bool IsIPv6Addr(ipaddr_t ip);

// Parses the `ip` address and returns an `in_addr` struct if successful.
// Otherwise, returns an `INADDR_ANY`.
struct in_addr ParseIPv4Addr(ipaddr_t ip);

// Parses the `ip` address and returns an `in6_addr` struct if successful.
// Otherwise, returns an `IN6ADDR_ANY_INIT`.
struct in6_addr ParseIPv6Addr(ipaddr_t ip);

// Returns a "ipv4:port" or "[ipv6]:port" string.
std::string ToIpAddrPortString(const struct sockaddr_storage& ss);

// Parses the `ip` address and `port` and returns a `sockaddr_in` struct.
// REQUIRE: `ip` is a valid IPv4 address.
struct sockaddr_in BuildIPv4Sockaddr(ipaddr_t ip, port_t port);

// Parses the `ip` address and `port` and returns a `sockaddr_in6` struct.
// REQUIRE: `ip` is a valid IPv6 address.
struct sockaddr_in6 BuildIPv6Sockaddr(ipaddr_t ip, port_t port);

}  // namespace peregrine

#endif  // PEREGRINE_SRC_INTERNAL_SOCKET_IP_UTIL_H_
