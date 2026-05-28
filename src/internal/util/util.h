#ifndef PEREGRINE_SRC_INTERNAL_UTIL_UTIL_H_
#define PEREGRINE_SRC_INTERNAL_UTIL_UTIL_H_

#include <sys/uio.h>

#include <cstddef>
#include <string>
#include <type_traits>

#include "src/internal/base/types.h"

namespace peregrine {

// Returns true iff the integer is a power of two.
template <typename T>
constexpr bool IsPowerOfTwo(T n) {
  static_assert(std::is_integral_v<T>);
  return n > 0 && (n & (n - 1)) == 0;
}

// Returns the total length of the `n` buffers.
size_t TotalLength(const IoVec* iov, int n);

// Returns the thread id where this function is called.
std::string ThreadId();

}  // namespace peregrine

#endif  // PEREGRINE_SRC_INTERNAL_UTIL_UTIL_H_
