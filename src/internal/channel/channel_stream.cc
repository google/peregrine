#include "src/internal/channel/channel_stream.h"

#include <cstddef>
#include <cstring>
#include <utility>

#include "absl/log/check.h"
#include "absl/synchronization/mutex.h"
#include "absl/types/span.h"
#include "src/api/types.h"
#include "src/internal/base/types.h"
#include "src/internal/util/test_iov.h"

namespace peregrine::internal::testing {

bool MemStreamChannel::Write(const absl::Span<const IoVec> iovecs) {
  DCHECK(!iovecs.empty());

  absl::MutexLock lock(mu_);
  for (const auto& v : iovecs) {
    // Do not merge multiple iovecs into a single one.
    OwnedIoVec owned_iov = TestOnly_Linearize({v});
    queue_.push_back(std::move(owned_iov));
  }
  return true;
}

ssize_t MemStreamChannel::Read(Byte* const buf, const size_t len) {
  OwnedIoVec owned_iov;
  {
    absl::MutexLock lock(mu_);
    if (queue_.empty()) {
      // CHECK(false) << "should block";
      return -1;
    }
    owned_iov = std::move(queue_.front());
    queue_.pop_front();
  }

  const Byte* const ptr = owned_iov.data.get();
  const size_t size = owned_iov.size;
  if (size <= 0) {
    return 0;
  } else if (size <= len) {
    std::memcpy(buf, ptr, size);
    return size;
  } else {
    std::memcpy(buf, ptr, len);
    const void* const p = ptr + len;
    const IoVec v(const_cast<void*>(p), size - len);
    OwnedIoVec iov = TestOnly_Linearize({v});
    {
      absl::MutexLock lock(mu_);
      queue_.push_front(std::move(iov));
    }
    return len;
  }
}

}  // namespace peregrine::internal::testing
