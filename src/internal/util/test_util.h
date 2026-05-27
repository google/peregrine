#ifndef PEREGRINE_SRC_INTERNAL_UTIL_TEST_UTIL_H_
#define PEREGRINE_SRC_INTERNAL_UTIL_TEST_UTIL_H_

#include "absl/strings/string_view.h"
#include "src/internal/base/types.h"

namespace peregrine::testing {

// end of file
inline constexpr iovec_t kEoF = {};
static_assert(kEoF.iov_base == nullptr && kEoF.iov_len == 0);

// ip addresses
inline constexpr absl::string_view kIPv4AnyAddr = "0.0.0.0";
inline constexpr absl::string_view kIPv6AnyAddr = "::";
inline constexpr absl::string_view kIPv4Localhost = "127.0.0.1";
inline constexpr absl::string_view kIPv6Localhost = "::1";

// Finds an unused TCP port in the given address `family`.
// Return a non-zero port if successful, otherwise crashes.
port_t TestOnly_FindFreeTcpPort(int family);

// Finds an unused UDP port in the given address `family`.
// Return a non-zero port if successful, otherwise crashes.
port_t TestOnly_FindFreeUdpPort(int family);

}  // namespace peregrine::testing

#endif  // PEREGRINE_SRC_INTERNAL_UTIL_TEST_UTIL_H_
