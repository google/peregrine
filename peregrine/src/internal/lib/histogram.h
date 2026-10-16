#ifndef PEREGRINE_SRC_INTERNAL_LIB_HISTOGRAM_H_
#define PEREGRINE_SRC_INTERNAL_LIB_HISTOGRAM_H_

#include <algorithm>
#include <cstddef>
#include <type_traits>
#include <utility>

#include "absl/container/inlined_vector.h"
#include "absl/log/check.h"

namespace peregrine::internal {

// This class implements a key-frequency map.
// It is thread-compatible but not thread-safe.
template <typename Key, typename Count, size_t kN = 4>
class Histogram {
  // Small, trivially copyable keys (e.g. integers) are passed by value;
  // all other keys (e.g. `std::string`) by const reference.
  using KeyArg = std::conditional_t<std::is_trivially_copyable_v<Key> &&
                                        sizeof(Key) <= 2 * sizeof(void*),
                                    Key, const Key&>;
  static_assert(std::is_integral_v<Count> && !std::is_same_v<Count, bool>,
                "Count must be an integer type.");
  static_assert(kN > 0, "kN must be positive.");

 public:
  // Returns the number of distinct keys in the histogram.
  size_t size() const { return entries_.size(); }

  // Returns an iterator to the first entry in the histogram.
  auto begin() const { return entries_.begin(); }

  // Returns an iterator to the end of the histogram.
  auto end() const { return entries_.end(); }

  // Returns the count for `key`, or 0 if `key` is not found.
  Count Get(KeyArg key) const {
    const auto it = find(key);
    return it != entries_.end() ? it->second : 0;
  }

  // Increments the count for `key` by `count`, which must be non-zero.
  void Add(KeyArg key, Count count) {
    DCHECK_NE(count, Count{0}) << "count must not be zero";
    // Fast path: consecutive adds of the same key hit the last entry in O(1).
    if (!entries_.empty() && entries_.back().first == key) {
      entries_.back().second += count;
    } else if (auto it = find(key); it != entries_.end()) {
      it->second += count;
    } else {
      entries_.push_back({key, count});
    }
  }

 private:
  using Entry = std::pair<Key, Count>;

  // Returns an iterator to the first entry with the given `key`, or
  // `entries_.end()` if not found.
  auto find(KeyArg key) { return find(entries_, key); }
  auto find(KeyArg key) const { return find(entries_, key); }

  // Shared implementation for the const and non-const overloads above.
  template <typename C>
  static auto find(C& entries, KeyArg key) {
    return std::find_if(entries.begin(), entries.end(),
                        [&key](const Entry& e) { return e.first == key; });
  }

 private:
  absl::InlinedVector<Entry, kN> entries_;
};

}  // namespace peregrine::internal

#endif  // PEREGRINE_SRC_INTERNAL_LIB_HISTOGRAM_H_
