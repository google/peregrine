#ifndef PEREGRINE_SRC_INTERNAL_CHANNEL_CHANNEL_STREAM_H_
#define PEREGRINE_SRC_INTERNAL_CHANNEL_CHANNEL_STREAM_H_

#include <cstddef>
#include <memory>
#include <string>

#include "absl/types/span.h"
#include "src/api/transport_types.h"
#include "src/internal/base/types.h"
#include "src/internal/channel/channel.h"
#include "src/internal/channel/channel_types.h"
#include "src/internal/channel/pipe.h"

namespace peregrine::internal::testing {

// A reliable memory channel to help testing: lossless, stream.
// It is thread-safe.
class MemStreamChannel final : public Channel {
 public:
  // Constructor for paired bidirectional pipes.
  explicit MemStreamChannel(const BidiPipe& bidi, int error_rate);

  // Returns the channel type.
  constexpr ChannelType Type() const override {
    return ChannelType::kReliableStream;
  }

  // Writes a number of buffers described by the `iovecs` to the channel.
  // Returns the number of bytes actually written if successful. Zero byte means
  // no data has been written due to non-error reasons. Returns -1 on error.
  ssize_t Write(absl::Span<const IoVec> iovecs) override;

  // Reads exactly `len` bytes of data into the `buf` from the the channel.
  // Returns the number of bytes actually read if successful. Returns 0 if
  // the peer side has closed the connection. Returns -1 on error.
  ssize_t Read(Byte* buf, size_t len) override;

  // Shuts down the channel so no more read/write calls.
  void Shutdown() override;

  // Returns a string representation for the channel.
  std::string ToString() const override;

 private:
  // Returns true iff the channel read/write should emulate an error.
  bool error() const;

 private:
  const int error_rate_;
  std::shared_ptr<MemPipe> in_pipe_;
  std::shared_ptr<MemPipe> out_pipe_;
};

}  // namespace peregrine::internal::testing

#endif  // PEREGRINE_SRC_INTERNAL_CHANNEL_CHANNEL_STREAM_H_
