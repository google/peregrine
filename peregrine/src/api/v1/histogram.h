#ifndef PEREGRINE_SRC_API_V1_HISTOGRAM_H_
#define PEREGRINE_SRC_API_V1_HISTOGRAM_H_

#include <cstddef>
#include <cstdint>

#include "peregrine/src/internal/lib/histogram.h"

namespace peregrine::v1 {

// A thread-compatible lightweight histogram.
template <typename Key, typename Count = uint32_t, size_t kN = 4>
using Histogram = internal::Histogram<Key, Count, kN>;

}  // namespace peregrine::v1

#endif  // PEREGRINE_SRC_API_V1_HISTOGRAM_H_
