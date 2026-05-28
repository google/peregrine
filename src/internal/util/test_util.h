#ifndef PEREGRINE_SRC_INTERNAL_UTIL_TEST_UTIL_H_
#define PEREGRINE_SRC_INTERNAL_UTIL_TEST_UTIL_H_

#include <memory>
#include <string_view>

#include "src/internal/base/types.h"
#include "src/internal/socket/socket_tcp.h"
#include "src/internal/socket/socket_udp.h"

namespace peregrine::internal::testing {

// end of file
inline constexpr IoVec kEoF = {};
static_assert(kEoF.iov_base == nullptr && kEoF.iov_len == 0);

// ip addresses
inline constexpr std::string_view kIPv4AnyAddr = "0.0.0.0";
inline constexpr std::string_view kIPv6AnyAddr = "::";
inline constexpr std::string_view kIPv4Localhost = "127.0.0.1";
inline constexpr std::string_view kIPv6Localhost = "::1";

// Finds an unused TCP port in the given address `family`.
// Return a non-zero port if successful, otherwise crashes.
port_t TestOnly_FindFreeTcpPort(int family);

// Finds an unused UDP port in the given address `family`.
// Return a non-zero port if successful, otherwise crashes.
port_t TestOnly_FindFreeUdpPort(int family);

// Creates a TCP socket in the given address `family`.
// Return a non-null socket if successful, otherwise crashes.
std::unique_ptr<TcpSocket> TestOnly_CreateTcpSocket(int family);

// Creates a UDP socket in the given address `family`.
// Return a non-null socket if successful, otherwise crashes.
std::unique_ptr<UdpSocket> TestOnly_CreateUdpSocket(int family);

}  // namespace peregrine::internal::testing

#endif  // PEREGRINE_SRC_INTERNAL_UTIL_TEST_UTIL_H_
