#ifndef PEREGRINE_SRC_INTERNAL_CHANNEL_CHANNEL_MSG_H_
#define PEREGRINE_SRC_INTERNAL_CHANNEL_CHANNEL_MSG_H_

#include <memory>
#include <string>

#include "absl/base/thread_annotations.h"
#include "absl/random/random.h"
#include "absl/strings/str_format.h"
#include "absl/synchronization/mutex.h"
#include "peregrine/src/internal/channel/channel.h"
#include "peregrine/src/internal/channel/channel_type.h"
#include "peregrine/src/internal/channel/pipe.h"
#include "peregrine/src/internal/lib/iovec_cursor.h"

namespace peregrine::internal::testing {

// An unreliable memory channel to help testing: lossless/lossy, message.
// It is thread-safe.
class MemMsgChannel final : public Channel {
 public:
  // Constructor.
  MemMsgChannel(const BidiPipe& bidi, bool blocking, int error_rate);

  // Returns the channel type.
  constexpr ChannelType Type() const override {
    constexpr ChannelType t = ChannelType::kMemMsg;
    static_assert(t.IsLossyChannel());
    static_assert(t.IsMessageChannel());
    static_assert(t.IsTwoSidedChannel());
    static_assert(t.IsFakeChannel());
    return t;
  }

  // Returns whether the channel is blocking or non-blocking.
  constexpr bool IsBlocking() const override { return blocking_; }

  // Writes the remaining buffers of the `iovecs` to the channel.
  // Returns the number of bytes actually written if successful. Zero byte means
  // no data has been written due to non-error reasons. Returns -1 on error.
  ssize_t Write(IoVecCursor& iovecs) override;

  // Reads one message of up to `iovecs.RemainingBytes()` from the channel into
  // the remaining buffers of the `iovecs`. Returns the number of bytes actually
  // read if successful. Returns 0 if the received packet has no payload.
  // Returns -1 on error.
  ssize_t Read(IoVecCursor& iovecs) override;

  // Shuts down the channel. After the channel is shutdown, write calls will
  // return -1, so no more data can be injected into the channel. Read calls
  // will continue to read the remaining data in the channel, if any.
  void Shutdown() override;

  // Returns a string representation for the channel.
  std::string ToString() const override {
    return absl::StrFormat("MemMsgChannel: error_rate=%d%%", error_rate_);
  }

 private:
  // Returns true iff the `in_pipe_` has data.
  bool hasIncomingData() const ABSL_EXCLUSIVE_LOCKS_REQUIRED(in_pipe_->mu);

  // Returns true iff a generated random number in [1, 100] is <= `threshold`.
  bool random(int threshold) ABSL_LOCKS_EXCLUDED(bitgen_mu_);

  // Returns true iff the channel read/write should emulate an error.
  bool error() { return error_rate_ > 0 && random(error_rate_); }

  // Returns true iff the channel read/write should emulate `EWOULDBLOCK`.
  bool wouldBlock() { return !blocking_ && random(10); }

 private:
  const bool blocking_;
  const int error_rate_;
  absl::Mutex bitgen_mu_;
  absl::BitGen bitgen_ ABSL_GUARDED_BY(bitgen_mu_);
  std::shared_ptr<MemPipe> in_pipe_;
  std::shared_ptr<MemPipe> out_pipe_;
};

}  // namespace peregrine::internal::testing

#endif  // PEREGRINE_SRC_INTERNAL_CHANNEL_CHANNEL_MSG_H_
