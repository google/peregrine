#include "src/internal/channel/channel_stream.h"

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

bool MemStreamChannel::Write(const absl::Span<const IoVec> iovecs) {
  DCHECK(!iovecs.empty());

  absl::MutexLock lock(out_pipe_->mu);
  if (out_pipe_->is_shutdown) {
    return false;
  }
  for (const auto& v : iovecs) {
    // Do not merge multiple iovecs into a single one.
    OwnedIoVec owned_iov = TestOnly_Linearize({v});
    out_pipe_->queue.push_back(std::move(owned_iov));
  }
  return true;
}

ssize_t MemStreamChannel::Read(Byte* const buf, const size_t len) {
  OwnedIoVec owned_iov;
  {
    absl::MutexLock lock(in_pipe_->mu);
    if (in_pipe_->queue.empty()) {
      if (in_pipe_->is_shutdown) {
        return 0;
      }
      // CHECK(false) << "should block";
      return -1;
    }
    owned_iov = std::move(in_pipe_->queue.front());
    in_pipe_->queue.pop_front();
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
      absl::MutexLock lock(in_pipe_->mu);
      in_pipe_->queue.push_front(std::move(iov));
    }
    return len;
  }
}

void MemStreamChannel::Shutdown() {
  out_pipe_->Shutdown();
  in_pipe_->Shutdown();
}

}  // namespace peregrine::internal::testing
