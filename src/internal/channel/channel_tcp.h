#ifndef PEREGRINE_SRC_INTERNAL_CHANNEL_CHANNEL_TCP_H_
#define PEREGRINE_SRC_INTERNAL_CHANNEL_CHANNEL_TCP_H_

#include <sys/socket.h>

#include <cstddef>
#include <memory>
#include <string>
#include <utility>

#include "absl/log/check.h"
#include "absl/types/span.h"
#include "src/api/types.h"
#include "src/internal/base/types.h"
#include "src/internal/channel/channel.h"
#include "src/internal/socket/socket_tcp.h"

namespace peregrine::internal {

// A tcp socket based channel: reliable, stream.
class TcpChannel final : public Channel {
 public:
  // Constructor.
  explicit TcpChannel(std::unique_ptr<TcpSocket> socket)
      : socket_(std::move(socket)) {
    DCHECK_NE(socket_, nullptr);
  }

  // Returns the channel type.
  constexpr ChannelType Type() const override {
    return ChannelType::kReliableStream;
  }

  // Writes a number of buffers described by the `iovecs` to the channel.
  // Returns true if all the data are written successfully. Otherwise,
  // returns false.
  bool Write(absl::Span<const IoVec> iovecs) override;

  // Reads exactly `len` bytes of data into the `buf` from the the channel.
  // Returns the number of bytes actually read if successful, or -1 otherwise.
  ssize_t Read(Byte* buf, size_t len) override {
    return socket_->Recv(buf, len);
  }

  // Returns a string representation for the channel.
  std::string ToString() const override;

 private:
  std::unique_ptr<TcpSocket> socket_;
};

}  // namespace peregrine::internal

#endif  // PEREGRINE_SRC_INTERNAL_CHANNEL_CHANNEL_TCP_H_
