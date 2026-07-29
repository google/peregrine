#ifndef PEREGRINE_SRC_INTERNAL_CHANNEL_CHANNEL_MSG_H_
#define PEREGRINE_SRC_INTERNAL_CHANNEL_CHANNEL_MSG_H_

#include <cstddef>
#include <memory>
#include <string>

#include "absl/strings/str_format.h"
#include "absl/types/span.h"
#include "src/api/transport_types.h"
#include "src/internal/base/types.h"
#include "src/internal/channel/channel.h"
#include "src/internal/channel/channel_types.h"
#include "src/internal/channel/pipe.h"

namespace peregrine::internal::testing {

// An unreliable memory channel to help testing: lossless/lossy, message.
// It is thread-safe.
class MemMsgChannel final : public Channel {
 public:
  // Constructor.
  explicit MemMsgChannel(const BidiPipe& bidi, int error_rate);

  // Returns the channel type.
  constexpr ChannelType Type() const override {
    return ChannelType::kUnreliableMessage;
  }

  // Writes a number of buffers described by the `iovecs` to the channel.
  // Returns true if all the data are written successfully, or false otherwise.
  bool Write(absl::Span<const IoVec> iovecs) override;

  // Reads a message of at most `len` bytes into the `buf` from the the channel.
  // Returns the number of bytes actually read if successful. Returns 0 if the
  // received packet has no payload. Returns -1 on error.
  virtual ssize_t Read(Byte* buf, size_t len) override;

  // Shuts down the channel so no more read/write calls.
  void Shutdown() override;

  // Returns a string representation for the channel.
  std::string ToString() const override {
    return absl::StrFormat("MemMsgChannel: error_rate=%d%%", error_rate_);
  }

 private:
  // Returns true iff the channel read/write should emulate an error.
  bool error() const;

 private:
  const int error_rate_;
  std::shared_ptr<MemPipe> in_pipe_;
  std::shared_ptr<MemPipe> out_pipe_;
};

}  // namespace peregrine::internal::testing

#endif  // PEREGRINE_SRC_INTERNAL_CHANNEL_CHANNEL_MSG_H_
