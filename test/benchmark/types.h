#ifndef PEREGRINE_TEST_BENCHMARK_TYPES_H_
#define PEREGRINE_TEST_BENCHMARK_TYPES_H_

#include <cstdint>
#include <ostream>
#include <string>

#include "absl/time/time.h"
#include "src/api/strong_int.h"

namespace peregrine::benchmark {

// Sender/receiver role.
enum class Role {
  kSndr,
  kRcvr,
};

// NIC bandwidth in Mbps.
DEFINE_STRONG_INT_TYPE(Mbps, int32_t);

inline constexpr Mbps kOneGbps = Mbps(1'000);

// Converts Mbps to Gbps.
constexpr float ToGbps(Mbps mbps) {
  return static_cast<float>(mbps.value()) / 1000.0f;
}

// Converts Gbps to Mbps.
template <typename T>
constexpr Mbps ToMbps(T gbps) {
  static_assert(std::is_integral_v<T> || std::is_floating_point_v<T>);
  return Mbps(static_cast<int32_t>(gbps * 1000.0));
}

// Returns a string representation for the rate in Mbps.
std::string ToString(Mbps mbps);

inline std::ostream& operator<<(std::ostream& os, const Mbps mbps) {
  return os << ToString(mbps);
}

// Returns a string representation for the number of bytes.
std::string ToString(uint64_t bytes);

// Calculates the rate in Mbps for the #bytes and interval.
Mbps CalcRate(uint64_t bytes, absl::Duration interval);

}  // namespace peregrine::benchmark

#endif  // PEREGRINE_TEST_BENCHMARK_TYPES_H_
