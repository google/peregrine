#include "src/internal/channel/channel_msg.h"

#include <algorithm>
#include <cstddef>
#include <cstring>
#include <utility>

#include "absl/log/check.h"
#include "absl/synchronization/mutex.h"
#include "absl/types/span.h"
#include "src/api/transport_types.h"
#include "src/internal/base/types.h"
#include "src/internal/util/test_iov.h"

namespace peregrine::internal::testing {

MemMsgChannel::MemMsgChannel(const int error_rate)
    : error_rate_(std::min(std::max(0, error_rate), 100)) {}

bool MemMsgChannel::Write(const absl::Span<const IoVec> iovecs) {
  DCHECK(!iovecs.empty());

  // To keep the message boundary, merge multiple iovecs into a single one.
  OwnedIoVec owned_iov = TestOnly_Linearize(iovecs);

  absl::MutexLock lock(mu_);
  queue_.push(std::move(owned_iov));
  return true;
}

ssize_t MemMsgChannel::Read(Byte* const buf, const size_t len) {
  OwnedIoVec owned_iov;
  {
    absl::MutexLock lock(mu_);
    if (queue_.empty()) {
      // CHECK(false) << "should block";
      return 0;
    }
    owned_iov = std::move(queue_.front());
    queue_.pop();

    if (error()) {
      return -1;
    }
  }

  const size_t size = owned_iov.size;
  if (size <= 0) {
    return 0;
  } else if (size > len) {
    return -1;
  } else {
    DCHECK(0 < size && size <= len);
    std::memcpy(buf, owned_iov.data.get(), size);
    return size;
  }
}

}  // namespace peregrine::internal::testing
