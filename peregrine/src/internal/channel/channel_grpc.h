#ifndef PEREGRINE_SRC_INTERNAL_CHANNEL_CHANNEL_GRPC_H_
#define PEREGRINE_SRC_INTERNAL_CHANNEL_CHANNEL_GRPC_H_

#include <sys/types.h>

#include <cstddef>
#include <memory>
#include <string>
#include <utility>

#include "absl/base/thread_annotations.h"
#include "absl/log/check.h"
#include "absl/synchronization/mutex.h"
#include "peregrine/src/internal/channel/channel.h"
#include "peregrine/src/internal/channel/channel_type.h"
#include "peregrine/src/internal/lib/iovec_cursor.h"
#include "peregrine/src/internal/socket/grpc_manager.h"

namespace peregrine::internal {

// A gRPC channel: reliable, message.
//
// A channel sends and receives data over a `GrpcStream`, which is one side of
// a bidirectional streaming gRPC call made by `GrpcManager`: each `Write()`
// sends one message and each `Read()` receives one message. The stream is
// asynchronous, and the channel turns its operations into blocking calls.
//
// `Write()` and `Shutdown()` are thread-safe. At most one thread can be in
// `Read()` at any time.
class GrpcChannel final : public Channel {
 public:
  // The max size of a message in bytes.
  static constexpr size_t kMaxMessageSize = GrpcStream::kMaxMessageSize;

  // Constructor.
  explicit GrpcChannel(std::unique_ptr<GrpcStream> stream)
      : sending_(false),
        send_failed_(false),
        shutdown_(false),
        stream_(std::move(stream)) {
    DCHECK_NE(stream_, nullptr);
  }

  // Returns the channel type.
  constexpr ChannelType Type() const override {
    constexpr ChannelType t = ChannelType::kGRPC;
    static_assert(t.IsLosslessChannel());
    static_assert(t.IsMessageChannel());
    static_assert(t.IsTwoSidedChannel());
    static_assert(t.IsRealChannel());
    return t;
  }

  // Returns whether the channel is blocking or non-blocking.
  constexpr bool IsBlocking() const override { return true; }

  // Writes the remaining buffers of the `iovecs` to the channel as one message
  // of up to `kMaxMessageSize` bytes. The message is copied and sent in the
  // background, so the call only blocks until the previous message is sent.
  // Returns the number of bytes written if successful. Returns -1 on error,
  // including the failure to send a previous message.
  ssize_t Write(IoVecCursor& iovecs) override;

  // Reads one message from the channel into the remaining buffers of the
  // `iovecs`, and blocks until a message is received. Returns the number of
  // bytes read if successful. Returns 0 if the channel is shut down or the peer
  // has closed it. Returns -1 if the message is bigger than the buffers.
  ssize_t Read(IoVecCursor& iovecs) override;

  // Shuts down the channel and unblocks the read/write calls in progress.
  // After the channel is shutdown, write calls will return -1 and read calls
  // will return 0. The peer can still read the messages written before the
  // shutdown.
  void Shutdown() override;

  // Returns a string representation for the channel.
  std::string ToString() const override;

 private:
  // Returns true iff `Write()` should stop waiting.
  bool canWrite() const ABSL_SHARED_LOCKS_REQUIRED(mu_);

 private:
  absl::Mutex mu_;
  bool sending_ ABSL_GUARDED_BY(mu_);
  bool send_failed_ ABSL_GUARDED_BY(mu_);
  bool shutdown_ ABSL_GUARDED_BY(mu_);

  const std::unique_ptr<GrpcStream> stream_;  // Must be destroyed first.
};

}  // namespace peregrine::internal

#endif  // PEREGRINE_SRC_INTERNAL_CHANNEL_CHANNEL_GRPC_H_
