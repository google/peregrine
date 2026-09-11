#ifndef PEREGRINE_SRC_INTERNAL_LIB_METRIC_HISTOGRAM_H_
#define PEREGRINE_SRC_INTERNAL_LIB_METRIC_HISTOGRAM_H_

#include <algorithm>
#include <array>
#include <bit>
#include <cstddef>
#include <cstdint>
#include <limits>

#include "absl/log/check.h"
#include "src/internal/lib/metric_counter.h"
#include "src/util/macro.h"

namespace peregrine::internal {

// A log2-spaced metric histogram supporting lock-free atomic updates.
//
// Bucket mapping:
//   - Bucket 0: value == 0
//   - Bucket i (1 <= i < kNumBuckets - 1): [2^(i-1), 2^i)
//   - Bucket kNumBuckets - 1: value >= 2^(kNumBuckets - 2) (overflow)
template <size_t kNumBuckets = 32>
class Log2Histogram {
  using SampleType = uint64_t;
  using CountType = uint64_t;

  static constexpr size_t kSampleBits = std::numeric_limits<SampleType>::digits;

 public:
  static_assert(kNumBuckets > 0, "kNumBuckets must be positive");
  static_assert(kNumBuckets <= kSampleBits,
                "kNumBuckets cannot exceed the number of bits in SampleType");

  // Default constructor.
  Log2Histogram() = default;

  DISALLOW_COPY(Log2Histogram);
  DISALLOW_MOVE(Log2Histogram);

  // Destructor.
  ~Log2Histogram() = default;

  // Records a sample value atomically with an optional count.
  void Record(SampleType sample, CountType count = 1) {
    sum_.Add(sample * count);
    size_t i = kSampleBits - std::countl_zero(sample);
    i = std::min(i, kNumBuckets - 1);
    buckets_[i].Add(count);
  }

  // Returns the total sum of recorded samples.
  SampleType Sum() const { return sum_.Value(); }

  // Returns the number of buckets.
  constexpr size_t NumBuckets() const { return kNumBuckets; }

  // Returns the count in bucket `i`.
  CountType Bucket(size_t i) const {
    DCHECK_LT(i, kNumBuckets);
    return buckets_[i].Value();
  }

  // Exports a snapshot of all bucket counts.
  // Not atomic across buckets, may race with concurrent updates.
  std::array<CountType, kNumBuckets> RacyExport() const {
    std::array<CountType, kNumBuckets> buckets;
    for (size_t i = 0; i < kNumBuckets; ++i) {
      buckets[i] = buckets_[i].Value();
    }
    return buckets;
  }

  // Sets all counters to zero.
  void Clear() {
    sum_.Clear();
    for (auto& b : buckets_) {
      b.Clear();
    }
  }

 private:
  MetricCounter<SampleType> sum_;
  MetricCounter<CountType> buckets_[kNumBuckets];
};

}  // namespace peregrine::internal

#endif  // PEREGRINE_SRC_INTERNAL_LIB_METRIC_HISTOGRAM_H_
