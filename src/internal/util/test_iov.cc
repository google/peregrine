#include "src/internal/util/test_iov.h"

#include <cstddef>
#include <cstring>
#include <memory>
#include <utility>

#include "absl/base/optimization.h"
#include "absl/log/check.h"
#include "absl/types/span.h"
#include "src/api/types.h"
#include "src/internal/base/types.h"
#include "src/internal/util/util.h"

namespace peregrine::internal::testing {

OwnedIoVec TestOnly_Linearize(const absl::Span<const IoVec> iovecs) {
  // Allocate a buffer to hold all the data.
  const size_t size = TotalLength(iovecs);
  if ABSL_PREDICT_FALSE (size <= 0) {
    return OwnedIoVec{.data = nullptr, .size = 0};
  }

  // Copy the data from the iovecs into the buffer.
  auto buf = std::make_unique_for_overwrite<Byte[]>(size);
  size_t offset = 0;
  for (const auto& v : iovecs) {
    void* __restrict const dst = buf.get() + offset;
    const void* __restrict const src = v.iov_base;
    const size_t n = v.iov_len;
    DCHECK(IsValid(v));
    if (src != nullptr && n > 0) {
      std::memcpy(dst, src, n);
      offset += n;
    }
  }
  DCHECK_EQ(offset, size);

  // Return the buffer, together with its ownership.
  return OwnedIoVec{.data = std::move(buf), .size = size};
}

}  // namespace peregrine::internal::testing
