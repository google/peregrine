#ifndef PEREGRINE_SRC_INTERNAL_CHANNEL_CHANNEL_STREAM_H_
#define PEREGRINE_SRC_INTERNAL_CHANNEL_CHANNEL_STREAM_H_

#include <cstddef>
#include <deque>
#include <string>

#include "absl/base/thread_annotations.h"
#include "absl/random/random.h"
#include "absl/synchronization/mutex.h"
#include "absl/types/span.h"
#include "src/api/types.h"
#include "src/internal/base/types.h"
#include "src/internal/channel/channel.h"
#include "src/internal/util/test_iov.h"

namespace peregrine::internal::testing {

// A reliable memory channel to help dev and test: lossless, stream.
// It is thread-safe.
class MemStreamChannel final : public Channel {
 public:
  // Returns the channel type.
  constexpr ChannelType Type() const override {
    return ChannelType::kReliableStream;
  }

  // Writes a number of buffers described by the `iovecs` to the channel.
  // Returns true if all the data are written successfully, or false otherwise.
  bool Write(absl::Span<const IoVec> iovecs) override ABSL_LOCKS_EXCLUDED(mu_);

  // Reads exactly `len` bytes of data into the `buf` from the the channel.
  // Returns the number of bytes actually read if successful. Returns 0 if
  // the peer side has closed the connection. Returns -1 on error.
  ssize_t Read(Byte* buf, size_t len) override ABSL_LOCKS_EXCLUDED(mu_);

  // Returns a string representation for the channel.
  std::string ToString() const override { return "MemStreamChannel"; }

 private:
  mutable absl::Mutex mu_;
  absl::BitGen bitgen_ ABSL_GUARDED_BY(mu_);
  std::deque<OwnedIoVec> queue_ ABSL_GUARDED_BY(mu_);
};

}  // namespace peregrine::internal::testing

#endif  // PEREGRINE_SRC_INTERNAL_CHANNEL_CHANNEL_STREAM_H_
