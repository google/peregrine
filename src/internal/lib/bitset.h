#ifndef PEREGRINE_SRC_INTERNAL_LIB_BITSET_H_
#define PEREGRINE_SRC_INTERNAL_LIB_BITSET_H_

#include <bit>
#include <bitset>
#include <cstddef>
#include <cstdint>
#include <ostream>
#include <string>
#include <vector>

#include "absl/log/check.h"
#include "src/internal/util/util.h"
#include "src/util/macro.h"

namespace peregrine::internal {

// This class implements a dynamic bitset whose size is fixed at construction
// time. The bitset is both copyable and movable.
// It is thread-compatible but not thread-safe.
class Bitset final {
 public:
  // Constructor with all bits cleared.
  explicit Bitset(uint32_t size) : size_(size), units_(numUnits(size_)) {
    DCHECK_GT(size_, 0);
    DCHECK_GT(units_.size(), 0);
    DCHECK(IsEmpty());  // Note: unused trailing bits are set to 0.
  }

  // Allows copy/move.
  ALLOW_COPY(Bitset);
  ALLOW_MOVE(Bitset);

  // Destructor.
  ~Bitset() = default;

  // Returns the size of the bitset.
  uint32_t Size() const { return size_; }

  // Returns the number of bits set.
  uint32_t Count() const;

  // Returns the bit at the index `i`.
  bool Get(uint32_t i) const {
    DCHECK_LT(i, size_);
    return units_[u(i)].bits.test(b(i));
  }

  // Sets the bit at the index `i` to 1.
  void Set(uint32_t i) {
    DCHECK_LT(i, size_);
    units_[u(i)].bits.set(b(i));
  }

  // Resets the bit at the index `i` to 0.
  void Reset(uint32_t i) {
    DCHECK_LT(i, size_);
    units_[u(i)].bits.reset(b(i));
  }

  // Returns true if all bits are 0.
  bool IsEmpty() const;

  // Returns true if all bits are 1.
  bool IsFull() const { return Count() == size_; }

  // Returns a string representation of the bitset.
  std::string ToString() const;

 private:
  // Returns the number of units needed to store `size` bits.
  static constexpr uint32_t numUnits(size_t size) {
    return (size + kUnitSize - 1) / kUnitSize;
  }

 private:
  static constexpr uint32_t kUnitSize = sizeof(size_t) * 8U;
  static constexpr uint32_t kUnitShift = std::bit_width(kUnitSize) - 1;
  static constexpr uint32_t kUnitBitMask = kUnitSize - 1;
  static_assert(IsPowerOfTwo(kUnitBitMask + 1));
  static_assert((1U << kUnitShift) == kUnitSize);

 private:
  struct Unit {
    std::bitset<kUnitSize> bits;
  };
  uint32_t u(uint32_t i) const { return i >> kUnitShift; }
  uint32_t b(uint32_t i) const { return i & kUnitBitMask; }

 private:
  uint32_t size_;
  std::vector<Unit> units_;
};

inline std::ostream& operator<<(std::ostream& os, const Bitset& b) {
  return os << b.ToString();
}

}  // namespace peregrine::internal

#endif  // PEREGRINE_SRC_INTERNAL_LIB_BITSET_H_
