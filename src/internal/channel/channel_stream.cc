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
    queue_.push(std::move(owned_iov));
  }
  return true;
}

bool MemStreamChannel::ReadExact(Byte* const buf, const size_t len) {
  OwnedIoVec owned_iov;
  {
    absl::MutexLock lock(mu_);
    if (queue_.empty()) {
      return false;
    }
    owned_iov = std::move(queue_.front());
    queue_.pop();
  }

  const size_t size = owned_iov.size;
  CHECK_EQ(size, len);  // Crash OK
  std::memcpy(buf, owned_iov.data.get(), size);
  return true;
}

}  // namespace peregrine::internal::testing
