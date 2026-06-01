#ifndef PEREGRINE_SRC_INTERNAL_BASE_TYPES_H_
#define PEREGRINE_SRC_INTERNAL_BASE_TYPES_H_

#include <netinet/in.h>
#include <sys/types.h>
#include <sys/uio.h>

#include <cstdint>
#include <string>
#include <variant>

#include "absl/log/check.h"

namespace peregrine::internal {

// hash value
using HashValue = uint64_t;

// io vector
using IoVec = ::iovec;

// tcp/udp port
using port_t = uint16_t;

// ip address
using ipv4_t = ::in_addr;
using ipv6_t = ::in6_addr;
using IpAddr = std::variant<ipv4_t, ipv6_t>;

// Returns true iff `ip` is an IPv4 address.
constexpr bool IsIPv4(const IpAddr& ip) {
  return std::holds_alternative<ipv4_t>(ip);
}

// Returns true iff `ip` is an IPv6 address.
constexpr bool IsIPv6(const IpAddr& ip) {
  return std::holds_alternative<ipv6_t>(ip);
}

// Returns the address family of `ip`.
constexpr int AddressFamily(const IpAddr& ip) {
  DCHECK(IsIPv4(ip) || IsIPv6(ip));
  return IsIPv4(ip) ? AF_INET : AF_INET6;
}

// Returns a string representation of the ipv4 address.
std::string ToIPv4String(const ipv4_t& ip4);

// Returns a string representation of the ipv6 address.
std::string ToIPv6String(const ipv6_t& ip6);

// Returns a string representation of the ip (v4 or v6) address.
std::string ToString(const IpAddr& ip);

}  // namespace peregrine::internal

#endif  // PEREGRINE_SRC_INTERNAL_BASE_TYPES_H_
