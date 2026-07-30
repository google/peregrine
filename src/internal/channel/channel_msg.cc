#include "src/internal/channel/channel_msg.h"

#include <algorithm>
#include <cstddef>
#include <cstring>
#include <utility>

#include "absl/base/optimization.h"
#include "absl/log/check.h"
#include "absl/synchronization/mutex.h"
#include "absl/types/span.h"
#include "src/api/transport_types.h"
#include "src/internal/base/types.h"
#include "src/internal/channel/pipe.h"
#include "src/internal/util/test_iov.h"
#include "src/internal/util/util.h"
#include "src/util/util.h"
#include "util/random/shared_bit_gen.h"

namespace peregrine::internal::testing {

MemMsgChannel::MemMsgChannel(const BidiPipe& bidi, const int error_rate)
    : error_rate_(std::min(std::max(0, error_rate), 100)),
      in_pipe_(bidi.InputPipe()),
      out_pipe_(bidi.OutputPipe()) {
  DCHECK_NE(in_pipe_, nullptr);
  DCHECK_NE(out_pipe_, nullptr);
}

bool MemMsgChannel::Write(const absl::Span<const IoVec> iovecs) {
  DCHECK(IsValid(iovecs));
  DCHECK_GE(TotalLength(iovecs), 1);

  // To keep the message boundary, merge multiple iovecs into a single one.
  OwnedIoVec owned_iov = TestOnly_Linearize(iovecs);

  absl::MutexLock lock(out_pipe_->mu);
  if (out_pipe_->shutdown) return false;
  out_pipe_->queue.push_back(std::move(owned_iov));
  return true;
}

ssize_t MemMsgChannel::Read(Byte* const buf, const size_t len) {
  DCHECK_NE(buf, nullptr);
  DCHECK_GE(len, 1);

  absl::MutexLock lock(in_pipe_->mu);
  if (in_pipe_->queue.empty()) {
    if (in_pipe_->shutdown) return 0;
    return -1;
  }

  OwnedIoVec owned_iov = std::move(in_pipe_->queue.front());
  in_pipe_->queue.pop_front();

  if (error()) {
    return -1;
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

void MemMsgChannel::Shutdown() {
  in_pipe_->Shutdown();
  out_pipe_->Shutdown();
}

bool MemMsgChannel::error() const {
  if ABSL_PREDICT_TRUE (error_rate_ <= 0) return false;
  util_random::SharedBitGen bitgen;
  return util::Random<int>(bitgen, 1, 100) <= error_rate_;
}

}  // namespace peregrine::internal::testing
