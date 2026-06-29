#ifndef PEREGRINE_SRC_UTIL_UTIL_H_
#define PEREGRINE_SRC_UTIL_UTIL_H_

#include <cstdint>
#include <limits>
#include <type_traits>

#include "absl/log/check.h"
#include "absl/random/bit_gen_ref.h"
#include "absl/random/random.h"
#include "absl/types/span.h"
#include "src/api/transport_types.h"
#include "xxhash/xxhash.h"

namespace peregrine::util {

// Generates a random integer in the range `[min, max]`, inclusively.
template <typename T>
T Random(absl::BitGenRef gen, T min = std::numeric_limits<T>::min(),
         T max = std::numeric_limits<T>::max()) {
  static_assert(std::is_integral_v<T>);
  DCHECK_LE(min, max);
  return absl::Uniform<T>(absl::IntervalClosedClosed, gen, min, max);
}

// Generates a random boolean value.
inline bool Toss(absl::BitGenRef bitgen) {
  return Random<uint8_t>(bitgen, 0, 1) == 0;
}

// Generates random non-zero bytes.
void RandomNonZero(absl::Span<Byte> data);

// Generates random non-zero bytes.
void RandomNonZero(absl::BitGenRef bitgen, absl::Span<Byte> data);

// Calculates the `xxHash64` for the data.
inline uint64_t CalcXxh64Hash(absl::Span<const Byte> data) {
  return XXH64(data.data(), data.size(), /*seed=*/0);
}

// Finds an unused port in the range [10,000, 65,535], inclusively. Returns
// the port number if successful, or 0 otherwise.
// Note: there is no guarantee that the found port is still available when
// the caller actually uses it.
uint16_t FindFreePort(int family, bool tcp);

}  // namespace peregrine::util

#endif  // PEREGRINE_SRC_UTIL_UTIL_H_
