#ifndef PEREGRINE_SRC_INTERNAL_UTIL_TEST_IOV_H_
#define PEREGRINE_SRC_INTERNAL_UTIL_TEST_IOV_H_

#include <sys/uio.h>

#include <cstddef>
#include <memory>

#include "absl/types/span.h"
#include "src/api/transport_types.h"
#include "src/internal/base/types.h"

namespace peregrine::internal::testing {

// `OwnedIoVec` describes a continuous buffer of data owned by the caller.
struct OwnedIoVec {
  std::unique_ptr<Byte[]> data;
  size_t size;
};

// Linearizes a sequence of `IoVecs` into a newly created contiguous buffer.
// Returns the buffer with its ownership moved to the caller.
OwnedIoVec TestOnly_Linearize(absl::Span<const IoVec> iovecs);

}  // namespace peregrine::internal::testing

#endif  // PEREGRINE_SRC_INTERNAL_UTIL_TEST_IOV_H_
