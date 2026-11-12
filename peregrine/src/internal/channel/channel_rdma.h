#ifndef PEREGRINE_SRC_INTERNAL_CHANNEL_CHANNEL_RDMA_H_
#define PEREGRINE_SRC_INTERNAL_CHANNEL_CHANNEL_RDMA_H_

#include <atomic>
#include <memory>
#include <string>
#include <utility>

#include "absl/log/check.h"
#include "peregrine/src/internal/channel/channel.h"
#include "peregrine/src/internal/channel/channel_type.h"
#include "peregrine/src/internal/lib/iovec_cursor.h"
#include "peregrine/src/internal/rdma/rdma_conn.h"

namespace peregrine::internal {

// An RDMA channel: reliable, message, one-sided.
// It is thread-compatible but not thread-safe.
class RdmaChannel final : public Channel {
 public:
  // Constructor.
  explicit RdmaChannel(std::unique_ptr<RdmaConn> rdma)
      : rdma_(std::move(rdma)), is_shutdown_(false) {
    DCHECK_NE(rdma_, nullptr);
    DCHECK_NE(rdma_->Qp(), nullptr);
  }

  // Returns the channel type.
  constexpr ChannelType Type() const override {
    constexpr ChannelType t = ChannelType::kRDMA;
    static_assert(t.IsLosslessChannel());
    static_assert(t.IsMessageChannel());
    static_assert(t.IsOneSidedChannel());
    static_assert(t.IsRealChannel());
    return t;
  }

  // Returns whether the channel is blocking or non-blocking.
  constexpr bool IsBlocking() const override { return true; }

  // Writes the remaining buffers of the `iovecs` to the channel.
  ssize_t Write(IoVecCursor& iovecs) override;

  // Reads data from the channel into the remaining buffers of the `iovecs`.
  ssize_t Read(IoVecCursor& iovecs) override;

  // Shuts down the channel and transitions the QP to error state.
  void Shutdown() override;

  // Returns a string representation for the channel.
  std::string ToString() const override;

 private:
  std::unique_ptr<RdmaConn> rdma_;
  std::atomic<bool> is_shutdown_;
};

}  // namespace peregrine::internal

#endif  // PEREGRINE_SRC_INTERNAL_CHANNEL_CHANNEL_RDMA_H_
