#include "src/internal/channel/channel_stream.h"

#include <algorithm>
#include <cstddef>
#include <cstring>
#include <string>
#include <utility>

#include "absl/base/optimization.h"
#include "absl/log/check.h"
#include "absl/strings/str_format.h"
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

MemStreamChannel::MemStreamChannel(const BidiPipe& bidi, const int error_rate)
    : error_rate_(std::clamp(error_rate, 0, 100)),
      in_pipe_(bidi.InputPipe()),
      out_pipe_(bidi.OutputPipe()) {
  DCHECK_NE(in_pipe_, nullptr);
  DCHECK_NE(out_pipe_, nullptr);
}

ssize_t MemStreamChannel::Write(const absl::Span<const IoVec> iovecs) {
  DCHECK(IsValid(iovecs));

  const size_t len = TotalLength(iovecs);
  DCHECK_GE(len, 1);

  absl::MutexLock lock(out_pipe_->mu);
  if (out_pipe_->shutdown) return -1;
  for (const auto& v : iovecs) {
    // Do not merge multiple iovecs into a single one.
    OwnedIoVec owned_iov = TestOnly_Linearize({v});
    out_pipe_->queue.push_back(std::move(owned_iov));
  }
  return len;
}

ssize_t MemStreamChannel::Read(Byte* const buf, const size_t len) {
  DCHECK_NE(buf, nullptr);
  DCHECK_GE(len, 1);

  absl::MutexLock lock(in_pipe_->mu);
  if (in_pipe_->queue.empty()) {
    if (in_pipe_->shutdown) return 0;
    // CHECK(false) << "should block";
    return -1;
  }

  if (error()) {
    return -1;
  }

  OwnedIoVec owned_iov = std::move(in_pipe_->queue.front());
  in_pipe_->queue.pop_front();

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
    in_pipe_->queue.push_front(std::move(iov));
    return len;
  }
}

void MemStreamChannel::Shutdown() {
  in_pipe_->Shutdown();
  out_pipe_->Shutdown();
}

std::string MemStreamChannel::ToString() const {
  return absl::StrFormat("MemStreamChannel: error_rate=%d%%", error_rate_);
}

bool MemStreamChannel::error() const {
  if ABSL_PREDICT_TRUE (error_rate_ <= 0) return false;
  util_random::SharedBitGen bitgen;
  return util::Random<int>(bitgen, 1, 100) <= error_rate_;
}

}  // namespace peregrine::internal::testing
