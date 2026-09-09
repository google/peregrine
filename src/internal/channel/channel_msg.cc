#include "src/internal/channel/channel_msg.h"

#include <algorithm>
#include <cstddef>
#include <cstring>
#include <utility>

#include "absl/base/optimization.h"
#include "absl/log/check.h"
#include "absl/random/random.h"
#include "absl/synchronization/mutex.h"
#include "absl/types/span.h"
#include "src/api/transport_types.h"
#include "src/internal/base/types.h"
#include "src/internal/channel/pipe.h"
#include "src/internal/util/test_iov.h"
#include "src/internal/util/util.h"
#include "src/util/util.h"

namespace peregrine::internal::testing {

MemMsgChannel::MemMsgChannel(const BidiPipe& bidi, const int error_rate)
    : error_rate_(std::clamp(error_rate, 0, 100)),
      in_pipe_(bidi.InputPipe()),
      out_pipe_(bidi.OutputPipe()) {
  DCHECK_NE(in_pipe_, nullptr);
  DCHECK_NE(out_pipe_, nullptr);
}

ssize_t MemMsgChannel::WriteV(const absl::Span<const IoVec> iovecs) {
  DCHECK(IsValid(iovecs));

  const size_t len = TotalLength(iovecs);
  DCHECK_GE(len, 1);

  // To keep the message boundary, merge multiple iovecs into a single one.
  OwnedIoVec owned_iov = TestOnly_Linearize(iovecs);

  absl::MutexLock lock(out_pipe_->mu);
  if (out_pipe_->shutdown) return -1;
  out_pipe_->queue.push_back(std::move(owned_iov));
  return len;
}

bool MemMsgChannel::hasIncomingData() const { return in_pipe_->HasData(); }

ssize_t MemMsgChannel::Read(Byte* const buf, const size_t len) {
  DCHECK_NE(buf, nullptr);
  DCHECK_GE(len, 1);

  absl::MutexLock lock(in_pipe_->mu);
  in_pipe_->mu.Await(absl::Condition(this, &MemMsgChannel::hasIncomingData));
  if (in_pipe_->queue.empty()) {
    DCHECK(in_pipe_->shutdown);
    return 0;
  }

  OwnedIoVec owned_iov = std::move(in_pipe_->queue.front());
  in_pipe_->queue.pop_front();
  if (error()) return -1;

  const size_t size = owned_iov.size;
  if (size == 0) {
    return 0;
  } else if (size > len) {
    return -1;
  } else {
    DCHECK(1 <= size && size <= len);
    std::memcpy(buf, owned_iov.data.get(), size);
    return size;
  }
}

ssize_t MemMsgChannel::ReadV(absl::Span<IoVec> iovecs) {
  DCHECK(IsValid(iovecs));
  DCHECK_GE(TotalLength(iovecs), 1);

  absl::MutexLock lock(in_pipe_->mu);
  in_pipe_->mu.Await(absl::Condition(this, &MemMsgChannel::hasIncomingData));
  if (in_pipe_->queue.empty()) {
    DCHECK(in_pipe_->shutdown);
    return 0;
  }

  OwnedIoVec owned_iov = std::move(in_pipe_->queue.front());
  in_pipe_->queue.pop_front();
  if (error()) return -1;

  size_t size = owned_iov.size;
  if (size == 0) return 0;
  if (size > TotalLength(iovecs)) return -1;

  size_t offset = 0;
  Byte* data = owned_iov.data.get();
  for (auto& iov : iovecs) {
    if (const size_t len = iov.iov_len; size <= len) {
      std::memcpy(iov.iov_base, data + offset, size);
      offset += size;
      size = 0;
      break;
    } else {
      std::memcpy(iov.iov_base, data + offset, len);
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

bool MemMsgChannel::error() const {
  if ABSL_PREDICT_TRUE (error_rate_ <= 0) return false;
  absl::BitGen bitgen;
  return util::Random<int>(bitgen, 1, 100) <= error_rate_;
}

}  // namespace peregrine::internal::testing
