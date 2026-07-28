#ifndef PEREGRINE_SRC_INTERNAL_CHANNEL_CHANNEL_STREAM_H_
#define PEREGRINE_SRC_INTERNAL_CHANNEL_CHANNEL_STREAM_H_

#include <cstddef>
#include <memory>
#include <string>
#include <utility>

#include "absl/log/check.h"
#include "absl/types/span.h"
#include "src/api/transport_types.h"
#include "src/internal/base/types.h"
#include "src/internal/channel/channel.h"
#include "src/internal/channel/channel_pipe.h"
#include "src/internal/channel/channel_types.h"

namespace peregrine::internal::testing {

// A reliable memory channel to help testing: lossless, stream.
// It is thread-safe.
class MemStreamChannel final : public Channel {
 public:
  // Constructor for paired bidirectional endpoints.
  explicit MemStreamChannel(BidiPipe bidi)
      : out_pipe_(std::move(bidi.out_pipe)), in_pipe_(std::move(bidi.in_pipe)) {
    DCHECK_NE(out_pipe_, nullptr);
    DCHECK_NE(in_pipe_, nullptr);
  }

  // Returns the channel type.
  constexpr ChannelType Type() const override {
    return ChannelType::kReliableStream;
  }

  // Writes a number of buffers described by the `iovecs` to the channel.
  // Returns true if all the data are written successfully, or false otherwise.
  bool Write(absl::Span<const IoVec> iovecs) override;

  // Reads exactly `len` bytes of data into the `buf` from the the channel.
  // Returns the number of bytes actually read if successful. Returns 0 if
  // the peer side has closed the connection. Returns -1 on error.
  ssize_t Read(Byte* buf, size_t len) override;

  // Shuts down the channel so no more read/write calls.
  void Shutdown() override;

  // Returns a string representation for the channel.
  std::string ToString() const override { return "MemStreamChannel"; }

 private:
  std::shared_ptr<MemPipe> out_pipe_;
  std::shared_ptr<MemPipe> in_pipe_;
};

}  // namespace peregrine::internal::testing

#endif  // PEREGRINE_SRC_INTERNAL_CHANNEL_CHANNEL_STREAM_H_
