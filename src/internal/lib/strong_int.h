#ifndef PEREGRINE_SRC_INTERNAL_LIB_STRONG_INT_H_
#define PEREGRINE_SRC_INTERNAL_LIB_STRONG_INT_H_

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <iosfwd>
#include <limits>
#include <ostream>
#include <string>
#include <type_traits>
#include <utility>

#include "absl/hash/hash.h"

namespace peregrine::internal {

// `StrongInt` is a wrapper around a native integer value type `T` with
// a `Tag` to distinguish between different integer types.
// It is thread-compatible but not thread-safe.
template <typename Tag, typename T>
class StrongInt final {
  static_assert(std::is_integral_v<T>);

 public:
  using ValueType = T;

  // Default constructor.
  constexpr StrongInt() noexcept : value_() {}

  // Constructor.
  explicit constexpr StrongInt(T v) noexcept : value_(v) {}

  // Returns the underlying value.
  constexpr T value() const { return value_; }

  // Comparisons.
  friend constexpr bool operator==(StrongInt a, StrongInt b) {
    return a.value_ == b.value_;
  }
  friend constexpr bool operator!=(StrongInt a, StrongInt b) {
    return a.value_ != b.value_;
  }
  friend constexpr bool operator<(StrongInt a, StrongInt b) {
    return a.value_ < b.value_;
  }
  friend constexpr bool operator>(StrongInt a, StrongInt b) {
    return a.value_ > b.value_;
  }
  friend constexpr bool operator<=(StrongInt a, StrongInt b) {
    return a.value_ <= b.value_;
  }
  friend constexpr bool operator>=(StrongInt a, StrongInt b) {
    return a.value_ >= b.value_;
  }

  // Returns a hash signature.
  size_t Hash() const { return Hash(*this); }

  // Returns a hash signature.
  static size_t Hash(StrongInt v) { return absl::Hash<StrongInt>{}(v); }

  // Returns the maximum value.
  static constexpr StrongInt max() {
    return StrongInt(std::numeric_limits<T>::max());
  }

  // Returns the minimum value.
  static constexpr StrongInt min() {
    return StrongInt(std::numeric_limits<T>::min());
  }

  // Returns a string representation.
  std::string ToString() const { return std::to_string(value_); }

 private:
  // Hash function.
  template <typename H>
  friend H AbslHashValue(H h, StrongInt v) {
    return H::combine(std::move(h), v.value_);
  }

 private:
  T value_;
};

template <typename Tag, typename T>
std::ostream& operator<<(std::ostream& os, StrongInt<Tag, T> v) {
  return os << v.value();
}
template <typename Tag>
std::ostream& operator<<(std::ostream& os, StrongInt<Tag, int8_t> v) {
  return os << static_cast<int>(v.value());
}
template <typename Tag>
std::ostream& operator<<(std::ostream& os, StrongInt<Tag, uint8_t> v) {
  return os << static_cast<unsigned int>(v.value());
}

#define DEFINE_STRONG_INT_TYPE(type_name, value_type, ...)          \
  struct type_name##_strong_int_tag_ {};                            \
  using type_name =                                                 \
      ::peregrine::internal::StrongInt<type_name##_strong_int_tag_, \
                                       value_type>;

}  // namespace peregrine::internal

#endif  // PEREGRINE_SRC_INTERNAL_LIB_STRONG_INT_H_
