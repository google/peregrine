#ifndef PEREGRINE_SRC_INTERNAL_CHANNEL_CHANNEL_RDMA_H_
#define PEREGRINE_SRC_INTERNAL_CHANNEL_CHANNEL_RDMA_H_

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>

#include "absl/log/log.h"
#include "absl/types/span.h"
#include "src/api/transport_types.h"
#include "src/internal/base/types.h"
#include "src/internal/channel/channel.h"
#include "src/internal/channel/channel_types.h"
#include "src/internal/rdma/rdma_queue_pair.h"
#include "src/util/macro.h"

namespace peregrine::internal {

// An RDMA Queue Pair based channel: reliable, message.
// Supports one-sided RDMA WRITE only.
// It is thread-compatible but not thread-safe.
class RdmaChannel final : public Channel {
 public:
  // Constructor.
  RdmaChannel(std::unique_ptr<RdmaQueuePair> qp, uint32_t lkey, uint32_t rkey);

  DISALLOW_COPY(RdmaChannel);
  DISALLOW_MOVE(RdmaChannel);

  // Destructor.
  ~RdmaChannel() override;

  // Returns the channel type.
  constexpr ChannelType Type() const override {
    return ChannelType::kReliableMessage;
  }

  // Single-buffer raw write is unsupported for one-sided RDMA channels because
  // the remote destination address is required via ChunkHeader.
  ssize_t Write(const Byte* /*buf*/, size_t /*len*/) override {
    LOG(ERROR)
        << "RdmaChannel::Write is unsupported; use WriteV with ChunkHeader";
    return -1;
  }

  // Writes a chunk via one-sided RDMA WRITE. Requires exactly 2 iovecs:
  // iovecs[0] is the serialized ChunkHeader containing the remote memory
  // address. iovecs[1] is the local payload buffer to transfer.
  ssize_t WriteV(absl::Span<const IoVec> iovecs) override;

  // Read operations are unsupported for one-sided RDMA channels.
  ssize_t Read(Byte* /*buf*/, size_t /*len*/) override {
    LOG(ERROR)
        << "RdmaChannel::Read is unsupported for one-sided RDMA channels";
    return -1;
  }

  // TODO: Implement ReadV for RDMA.
  ssize_t ReadV(absl::Span<IoVec> iovecs) override;

  // Shuts down the channel and transitions the QP to error state.
  void Shutdown() override;

  // Returns a string representation for the channel.
  std::string ToString() const override;

 private:
  std::unique_ptr<RdmaQueuePair> qp_;
  const uint32_t lkey_;
  const uint32_t rkey_;
  std::atomic<bool> is_shutdown_;
};

}  // namespace peregrine::internal

#endif  // PEREGRINE_SRC_INTERNAL_CHANNEL_CHANNEL_RDMA_H_
