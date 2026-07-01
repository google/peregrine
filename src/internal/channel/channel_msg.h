#ifndef PEREGRINE_SRC_INTERNAL_CHANNEL_CHANNEL_MSG_H_
#define PEREGRINE_SRC_INTERNAL_CHANNEL_CHANNEL_MSG_H_

#include <cstddef>
#include <queue>
#include <string>

#include "absl/base/thread_annotations.h"
#include "absl/random/random.h"
#include "absl/strings/str_format.h"
#include "absl/synchronization/mutex.h"
#include "absl/types/span.h"
#include "src/api/transport_types.h"
#include "src/internal/base/types.h"
#include "src/internal/channel/channel.h"
#include "src/internal/channel/channel_types.h"
#include "src/internal/util/test_iov.h"
#include "src/util/util.h"

namespace peregrine::internal::testing {

// An unreliable memory channel to help testing: lossless/lossy, message.
// It is thread-safe.
class MemMsgChannel final : public Channel {
 public:
  // Constructor.
  explicit MemMsgChannel(int error_rate);

  // Returns the channel type.
  constexpr ChannelType Type() const override {
    return ChannelType::kUnreliableMessage;
  }

  // Writes a number of buffers described by the `iovecs` to the channel.
  // Returns true if all the data are written successfully, or false otherwise.
  bool Write(absl::Span<const IoVec> iovecs) override ABSL_LOCKS_EXCLUDED(mu_);

  // Reads a message of at most `len` bytes into the `buf` from the the channel.
  // Returns the number of bytes actually read if successful. Returns 0 if the
  // received packet has no payload. Returns -1 on error.
  virtual ssize_t Read(Byte* buf, size_t len) override ABSL_LOCKS_EXCLUDED(mu_);

  // Shuts down the channel so no more read/write calls.
  void Shutdown() override {}

  // Returns a string representation for the channel.
  std::string ToString() const override {
    return absl::StrFormat("MemMsgChannel: error_rate=%d%%", error_rate_);
  }

 private:
  // Returns true iff the channel read/write should emulate an error.
  bool error() ABSL_EXCLUSIVE_LOCKS_REQUIRED(mu_) {
    return util::Random<int>(bitgen_, 1, 100) <= error_rate_;
  }

 private:
  const int error_rate_;

  mutable absl::Mutex mu_;
  absl::BitGen bitgen_ ABSL_GUARDED_BY(mu_);
  std::queue<OwnedIoVec> queue_ ABSL_GUARDED_BY(mu_);
};

}  // namespace peregrine::internal::testing

#endif  // PEREGRINE_SRC_INTERNAL_CHANNEL_CHANNEL_MSG_H_
