#ifndef PEREGRINE_SRC_INTERNAL_UTIL_TEST_UTIL_H_
#define PEREGRINE_SRC_INTERNAL_UTIL_TEST_UTIL_H_

#include <memory>
#include <string_view>

#include "src/internal/base/endpoint.h"
#include "src/internal/base/hostinfo.h"
#include "src/internal/base/types.h"
#include "src/internal/socket/socket_tcp.h"
#include "src/internal/socket/socket_udp.h"
#include "src/util/ipaddr.h"

namespace peregrine::internal::testing {

// ip addresses
inline constexpr std::string_view kIPv4AnyAddr = "0.0.0.0";
inline constexpr std::string_view kIPv6AnyAddr = "::";
inline constexpr std::string_view kIPv4Localhost = "127.0.0.1";
inline constexpr std::string_view kIPv6Localhost = "::1";

// Returns the ipv4 `ANY_ADDR` (all 0's).
inline util::IpAddr IPv4AnyAddr() {
  return util::IpAddr(util::ParseIPv4Addr(kIPv4AnyAddr).value());
}

// Returns the ipv6 `ANY_ADDR` (all 0's).
inline util::IpAddr IPv6AnyAddr() {
  return util::IpAddr(util::ParseIPv6Addr(kIPv6AnyAddr).value());
}

// Returns the ipv4 localhost address.
inline util::IpAddr IPv4Localhost() {
  return util::IpAddr(util::ParseIPv4Addr(kIPv4Localhost).value());
}

// Returns the ipv6 localhost address.
inline util::IpAddr IPv6Localhost() {
  return util::IpAddr(util::ParseIPv6Addr(kIPv6Localhost).value());
}

// Returns an ipv4 or ipv6 localhost address in the given address `family`.
inline util::IpAddr IpLocalhost(int family) {
  return family == AF_INET ? IPv4Localhost() : IPv6Localhost();
}

// Creates a localhost endpoint in the given address `family` and protocol.
Endpoint TestOnly_LocalEndpoint(int family, bool tcp);

// Creates localhost host info in the given address `family` and protocol.
HostInfo TestOnly_LocalHostInfo(int family, bool tcp);

// Creates localhost host info in the given address `family` and protocol.
HostInfo TestOnly_LocalHostInfoWithZeroDataPlanePorts(int family, bool tcp);

// Finds an unused TCP port in the given address `family`.
// Return a nonzero port if successful, otherwise crashes.
port_t TestOnly_FindFreeTcpPort(int family);

// Finds an unused UDP port in the given address `family`.
// Return a nonzero port if successful, otherwise crashes.
port_t TestOnly_FindFreeUdpPort(int family);

// Creates a TCP socket in the given address `family`.
// Return a non-null socket if successful, otherwise crashes.
std::unique_ptr<TcpSocket> TestOnly_CreateTcpSocket(int family);

// Creates a UDP socket in the given address `family`.
// Return a non-null socket if successful, otherwise crashes.
std::unique_ptr<UdpSocket> TestOnly_CreateUdpSocket(int family);

}  // namespace peregrine::internal::testing

#endif  // PEREGRINE_SRC_INTERNAL_UTIL_TEST_UTIL_H_
