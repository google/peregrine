#ifndef PEREGRINE_SRC_INTERNAL_CHANNEL_CHANNEL_RDMA_H_
#define PEREGRINE_SRC_INTERNAL_CHANNEL_CHANNEL_RDMA_H_

#include <atomic>
#include <cstdint>
#include <memory>
#include <string>

#include "absl/types/span.h"
#include "peregrine/src/internal/base/types.h"
#include "peregrine/src/internal/channel/channel.h"
#include "peregrine/src/internal/channel/channel_type.h"
#include "peregrine/src/internal/rdma/rdma_qpair.h"

namespace peregrine::internal {

// An RDMA Queue Pair based channel: reliable, message.
// Supports one-sided RDMA WRITE only.
// It is thread-compatible but not thread-safe.
class RdmaChannel final : public Channel {
 public:
  // Constructor.
  RdmaChannel(std::unique_ptr<RdmaQPair> qp, uint32_t lkey, uint32_t rkey);

  // Destructor.
  ~RdmaChannel() override;

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

  // Writes a chunk via one-sided RDMA WRITE. Requires exactly 2 iovecs:
  // iovecs[0] is the serialized ChunkHeader containing the remote memory
  // address. iovecs[1] is the local payload buffer to transfer.
  ssize_t Write(absl::Span<const IoVec> iovecs) override;

  // TODO: Implement ReadV for RDMA.
  ssize_t Read(absl::Span<IoVec> iovecs) override;

  // Shuts down the channel and transitions the QP to error state.
  void Shutdown() override;

  // Returns a string representation for the channel.
  std::string ToString() const override;

 private:
  std::unique_ptr<RdmaQPair> qp_;
  const uint32_t lkey_;
  const uint32_t rkey_;
  std::atomic<bool> is_shutdown_;
};

}  // namespace peregrine::internal

#endif  // PEREGRINE_SRC_INTERNAL_CHANNEL_CHANNEL_RDMA_H_
