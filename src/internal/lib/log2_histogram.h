#ifndef PEREGRINE_SRC_INTERNAL_LIB_LOG2_HISTOGRAM_H_
#define PEREGRINE_SRC_INTERNAL_LIB_LOG2_HISTOGRAM_H_

#include <algorithm>
#include <array>
#include <bit>
#include <cstdint>
#include <limits>
#include <type_traits>

#include "absl/log/check.h"
#include "src/internal/lib/metric_counter.h"
#include "src/util/macro.h"

namespace peregrine::internal {

// A log2-spaced metric histogram supporting lock-free atomic updates.
//
// Sample value vs bucket mapping (N = #buckets):
//    Value Rrange      Bucket
//    [0, 1)            0
//    [1, 2)            1
//    [2, 4)            2
//    ...
//    [2^(i-1), 2^i)    i
//    ...
//    [2^(N-2), inf)    N-1
template <int /*num_buckets*/ N = 32>
class Log2Histogram {
  using SampleT = uint64_t;
  using CountT = uint64_t;
  static_assert(std::is_unsigned_v<CountT> && std::is_integral_v<CountT>);
  static_assert(std::is_unsigned_v<SampleT> && std::is_integral_v<SampleT>);
  static constexpr int kSampleBits = std::numeric_limits<SampleT>::digits;
  static_assert(1 <= N && N <= kSampleBits);

 public:
  // Default constructor.
  Log2Histogram() = default;

  // Disallows copy and move.
  DISALLOW_COPY(Log2Histogram);
  DISALLOW_MOVE(Log2Histogram);

  // Destructor.
  ~Log2Histogram() = default;

  // Returns the number of buckets.
  constexpr int NumBuckets() const { return N; }

  // Returns the total sum of recorded sample values.
  SampleT Sum() const { return sum_.Value(); }

  // Returns the `i`-th bucket count. Returns 0 if `i` is out of bounds.
  CountT Bucket(uint32_t i) const { return i < N ? buckets_[i].Value() : 0; }

  // Records a sample value atomically with an optional count.
  void Record(SampleT sample, CountT count = 1) {
    sum_.Add(sample * count);
    const int i = ToBucket(sample);
    DCHECK(0 <= i && i <= N - 1);
    buckets_[i].Add(count);
  }

  // Exports a snapshot of all bucket counts.
  // Not atomic across buckets, may race with concurrent updates.
  std::array<CountT, N> RacyExport() const {
    std::array<CountT, N> buckets;
    for (int i = 0; i < N; ++i) {
      buckets[i] = buckets_[i].Value();
    }
    return buckets;
  }

  // Sets all counters to zero.
  void Clear() {
    sum_.Clear();
    for (auto& b : buckets_) b.Clear();
  }

 private:
  // Returns the bucket index for the given sample value.
  constexpr int ToBucket(SampleT sample) const {
    static_assert(std::is_unsigned_v<SampleT>);
    const int z = std::countl_zero(sample);
    DCHECK(0 <= z && z <= kSampleBits);
    return std::min(kSampleBits - z, N - 1);
  }

 private:
  MetricCounter<SampleT> sum_;
  std::array<MetricCounter<CountT>, N> buckets_;
};

}  // namespace peregrine::internal

#endif  // PEREGRINE_SRC_INTERNAL_LIB_LOG2_HISTOGRAM_H_
