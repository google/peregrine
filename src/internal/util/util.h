#ifndef PEREGRINE_SRC_INTERNAL_UTIL_UTIL_H_
#define PEREGRINE_SRC_INTERNAL_UTIL_UTIL_H_

#include <sys/uio.h>

#include <cstddef>
#include <string>
#include <type_traits>
#include <utility>

#include "absl/types/span.h"
#include "src/api/transport_types.h"
#include "src/internal/base/types.h"

namespace peregrine::internal {

// Returns true iff the integer is a power of two.
template <typename T>
constexpr bool IsPowerOfTwo(T n) {
  static_assert(std::is_integral_v<T>);
  return n > 0 && (n & (n - 1)) == 0;
}

// end of file
inline constexpr IoVec kEoF = {};
static_assert(kEoF.iov_base == nullptr && kEoF.iov_len == 0);

// Returns the `IoVec`'s buffer pointer and length as a pair.
inline std::pair<Byte*, size_t> BufLen(const IoVec& iov) {
  return {reinterpret_cast<Byte*>(iov.iov_base), iov.iov_len};
}

// Returns true iff the `IoVec` is valid.
inline bool IsValid(const IoVec& v) {
  return v.iov_base != nullptr && v.iov_len > 0;
}

// Returns true iff all the `iovecs` are valid.
bool IsValid(absl::Span<const IoVec> iovecs);

// Returns the total length of the `iovecs`.
size_t TotalLength(absl::Span<const IoVec> iovecs);

// Returns the total length of the `n` iovecs.
inline size_t TotalLength(const IoVec* iov, int n) {
  return TotalLength(absl::MakeSpan(iov, n));
}

// Returns the thread id where this function is called.
std::string ThreadId();

}  // namespace peregrine::internal

#endif  // PEREGRINE_SRC_INTERNAL_UTIL_UTIL_H_
