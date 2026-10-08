#include "peregrine/src/internal/util/test_iov.h"

#include <algorithm>
#include <cstddef>
#include <cstring>
#include <memory>
#include <utility>
#include <vector>

#include "absl/base/optimization.h"
#include "absl/log/check.h"
#include "absl/types/span.h"
#include "peregrine/src/api/transport_types.h"
#include "peregrine/src/internal/base/types.h"
#include "peregrine/src/internal/util/util.h"

namespace peregrine::internal::testing {

OwnedIoVec TestOnly_Linearize(const absl::Span<const IoVec> iovecs) {
  // Calculate the total length of all the iovecs.
  const size_t size = TotalLength(iovecs);
  if ABSL_PREDICT_FALSE (size <= 0) {
    return OwnedIoVec{.data = nullptr, .size = 0};
  }

  // Allocate a buffer to hold all the data.
  auto buf = std::make_unique_for_overwrite<Byte[]>(size);
  DCHECK_NE(buf, nullptr);

  // Copy the data from the iovecs into the buffer.
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

std::vector<struct iovec> TestOnly_Split(absl::Span<Byte> buf, size_t n) {
  const size_t size = buf.size();
  DCHECK_GE(size, 1);

  n = std::clamp(n, 1UL, size);
  const size_t partial = size / n;
  DCHECK_GE(partial, 1);

  std::vector<struct iovec> iovs;
  iovs.reserve(n);
  size_t offset = 0;
  for (size_t i = 0; i < n - 1; ++i) {
    iovs.push_back({buf.data() + offset, partial});
    offset += partial;
  }
  DCHECK_LE(offset + partial, size);
  iovs.push_back({buf.data() + offset, size - offset});
  return iovs;
}

}  // namespace peregrine::internal::testing
