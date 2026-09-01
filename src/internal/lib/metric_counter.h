#ifndef PEREGRINE_SRC_INTERNAL_LIB_METRIC_COUNTER_H_
#define PEREGRINE_SRC_INTERNAL_LIB_METRIC_COUNTER_H_

#include <atomic>
#include <type_traits>

#include "src/util/macro.h"

namespace peregrine::internal {

// A metric counter that does not have to be precise.
// Lock-free concurrent updates are supported in a lossy manner.
template <typename T>
class MetricCounter {
  static_assert(std::is_integral_v<T>);

 public:
  // Default constructor.
  constexpr MetricCounter() : value_(0) {}

  // Disallow copy and move.
  DISALLOW_COPY(MetricCounter);
  DISALLOW_MOVE(MetricCounter);

  // Destructor.
  ~MetricCounter() = default;

  // Return the current value of the counter.
  T Value() const { return get(); }

  // Sets the counter to zero.
  void Clear() { set(0); }

  // Add `n` to the metric counter in a lossy manner.
  // Counts may be lost if this function is called concurrently.
  void Add(T n) { set(get() + n); }

 private:
  // Returns the current value of the counter.
  T get() const { return value_.load(std::memory_order_relaxed); }

  // Sets the value of the counter to `v`.
  void set(T v) { value_.store(v, std::memory_order_relaxed); }

 private:
  std::atomic<T> value_;
};

}  // namespace peregrine::internal

#endif  // PEREGRINE_SRC_INTERNAL_LIB_METRIC_COUNTER_H_
