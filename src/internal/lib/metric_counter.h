#ifndef PEREGRINE_SRC_INTERNAL_LIB_METRIC_COUNTER_H_
#define PEREGRINE_SRC_INTERNAL_LIB_METRIC_COUNTER_H_

#include <atomic>
#include <type_traits>

#include "src/util/macro.h"

namespace peregrine::internal {

// A metric counter supporting lock-free atomic or lossy updates.
template <typename T>
class MetricCounter {
  static_assert(std::is_integral_v<T>);

 public:
  // Constructor.
  explicit constexpr MetricCounter(T v = 0) : value_(v) {}

  // Disallow copy and move.
  DISALLOW_COPY(MetricCounter);
  DISALLOW_MOVE(MetricCounter);

  // Destructor.
  ~MetricCounter() = default;

  // Returns the current counter value.
  T Value() const { return get(); }

  // Adds `n` to the counter atomically.
  void Add(T n) { fetch_add(n); }

  // Adds `n` to the counter in a lossy manner.
  // Counts may be lost if this function is called concurrently.
  void LossyAdd(T n) { set(get() + n); }

  // Sets the counter to zero.
  void Clear() { set(0); }

 private:
  T get() const { return value_.load(std::memory_order_relaxed); }
  void set(T v) { value_.store(v, std::memory_order_relaxed); }
  void fetch_add(T n) { value_.fetch_add(n, std::memory_order_relaxed); }

 private:
  std::atomic<T> value_;
};

}  // namespace peregrine::internal

#endif  // PEREGRINE_SRC_INTERNAL_LIB_METRIC_COUNTER_H_
