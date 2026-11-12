#include "peregrine/src/internal/channel/channel_msg.h"

#include <algorithm>
#include <cstddef>
#include <cstring>
#include <utility>

#include "absl/log/check.h"
#include "absl/synchronization/mutex.h"
#include "peregrine/src/api/transport_types.h"
#include "peregrine/src/internal/channel/pipe.h"
#include "peregrine/src/internal/lib/iovec_cursor.h"
#include "peregrine/src/internal/util/test_iov.h"
#include "peregrine/src/util/util.h"

namespace peregrine::internal::testing {

MemMsgChannel::MemMsgChannel(const BidiPipe& bidi, const bool blocking,
                             const int error_rate)
    : blocking_(blocking),
      error_rate_(std::clamp(error_rate, 0, 100)),
      in_pipe_(bidi.InputPipe()),
      out_pipe_(bidi.OutputPipe()) {
  DCHECK_NE(in_pipe_, nullptr);
  DCHECK_NE(out_pipe_, nullptr);
}

ssize_t MemMsgChannel::Write(IoVecCursor& iovecs) {
  DCHECK(iovecs.IsValid());

  if (wouldBlock()) return 0;

  const size_t all = iovecs.RemainingBytes();
  DCHECK_GE(all, 1);
  DCHECK_EQ(all, iovecs.TotalBytes());

  // To keep the message boundary, merge multiple iovecs into a single one.
  const auto iovs = iovecs.RemainingSpan();
  OwnedIoVec owned_iov = TestOnly_Linearize(iovs);
  {
    absl::MutexLock lock(out_pipe_->mu);
    if (out_pipe_->shutdown) return -1;
    out_pipe_->queue.push_back(std::move(owned_iov));
  }
  iovecs.Advance(all);
  return all;
}

bool MemMsgChannel::hasIncomingData() const { return in_pipe_->HasData(); }

ssize_t MemMsgChannel::Read(IoVecCursor& iovecs) {
  DCHECK(iovecs.IsValid());

  const size_t all = iovecs.RemainingBytes();
  DCHECK_GE(all, 1);
  DCHECK_EQ(all, iovecs.TotalBytes());

  absl::MutexLock lock(in_pipe_->mu);
  if (blocking_) {
    in_pipe_->mu.Await(absl::Condition(this, &MemMsgChannel::hasIncomingData));
  }
  if (in_pipe_->queue.empty()) {
    DCHECK(in_pipe_->shutdown || !blocking_);
    return 0;
  }
  if (wouldBlock()) return 0;

  OwnedIoVec owned_iov = std::move(in_pipe_->queue.front());
  in_pipe_->queue.pop_front();
  if (error()) return -1;

  size_t size = owned_iov.size;
  if (size == 0) return 0;
  if (size > iovecs.RemainingBytes()) return -1;

  size_t offset = 0;
  Byte* data = owned_iov.data.get();
  for (auto& iov : iovecs.RemainingSpan()) {
    if (const size_t len = iov.iov_len; size <= len) {
      std::memcpy(iov.iov_base, data + offset, size);
      iovecs.Advance(size);
      offset += size;
      size = 0;
      break;
    } else {
      std::memcpy(iov.iov_base, data + offset, len);
      iovecs.Advance(len);
      offset += len;
      size -= len;
    }
  }
  DCHECK_EQ(size, 0);
  return offset;
}

void MemMsgChannel::Shutdown() {
  in_pipe_->Shutdown();
  out_pipe_->Shutdown();
}

bool MemMsgChannel::random(const int threshold) {
  absl::MutexLock _(bitgen_mu_);
  return util::Random<int>(bitgen_, 1, 100) <= threshold;
}

}  // namespace peregrine::internal::testing
