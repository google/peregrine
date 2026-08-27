#ifndef PEREGRINE_SRC_INTERNAL_RDMA_RDMA_QUEUE_PAIR_H_
#define PEREGRINE_SRC_INTERNAL_RDMA_RDMA_QUEUE_PAIR_H_

#include <infiniband/verbs.h>

#include <cstdint>
#include <memory>

#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "src/internal/rdma/rdma_device_context.h"
#include "src/util/macro.h"

namespace peregrine::internal {

// This class represents a Reliable Connected (RC) RDMA Queue Pair.
// It manages QP lifecycle, dedicated hardware Completion Queue (CQ) ownership,
// hardware resource binding to an RdmaDeviceContext, and state transitions
// (RESET -> INIT -> RTR -> RTS).
//
// It is thread-compatible but not thread-safe.
class RdmaQueuePair final {
 public:
  // Configuration options for creating and initializing the Queue Pair.
  // TODO: Allow these options to be configured via external config / flags.
  struct Options {
    uint32_t max_send_wr = 1024;
    uint32_t max_recv_wr = 1024;
    uint32_t max_send_sge = 1;
    uint32_t max_recv_sge = 1;
    uint32_t cq_size = 2048;  // Dedicated CQ entries
    enum ibv_mtu path_mtu = IBV_MTU_1024;
    uint8_t hop_limit = 255;    // IP Time-To-Live
    uint8_t traffic_class = 0;  // IP DSCP / QoS priority bits
    uint8_t sl = 0;             // InfiniBand Service Level
    int access_flags = IBV_ACCESS_LOCAL_WRITE | IBV_ACCESS_REMOTE_WRITE |
                       IBV_ACCESS_REMOTE_READ | IBV_ACCESS_REMOTE_ATOMIC;
    uint8_t timeout = 14;   // ~67.1 ms (4.096 us * 2^14)
    uint8_t retry_cnt = 7;  // Maximum 7 retries
    uint8_t rnr_retry = 7;  // Maximum 7 RNR retries (7 = infinite/max)
    uint8_t max_rd_atomic = 16;
    uint8_t max_dest_rd_atomic = 16;
    uint8_t min_rnr_timer = 12;
    bool sq_sig_all = false;  // False = only signal WRs with IBV_SEND_SIGNALED
  };

  // Creates and initializes an RC Queue Pair on the specified device. The QP is
  // allocated its own private Completion Queue and automatically transitioned
  // into INIT state.
  // Returns nullptr or an error status on creation failure.
  static absl::StatusOr<std::unique_ptr<RdmaQueuePair>> Create(
      RdmaDeviceContext* device_context, const Options& options);
  static absl::StatusOr<std::unique_ptr<RdmaQueuePair>> Create(
      RdmaDeviceContext* device_context) {
    return Create(device_context, Options{});
  }

  DISALLOW_COPY(RdmaQueuePair);
  DISALLOW_MOVE(RdmaQueuePair);

  // Destructor. Destroys the underlying ibv_qp and private ibv_cq.
  ~RdmaQueuePair();

  // Connects the QP to the remote peer (transitions INIT -> RTR -> RTS).
  absl::Status Connect(uint32_t remote_qpn, const union ibv_gid& remote_gid,
                       uint32_t remote_psn = 0, uint32_t local_psn = 0);

  // Returns true iff the QP has completed handshake and is in RTS state.
  bool IsConnected() const { return state_ == State::kRts; }

  // Queries and returns the local GID for this device's port.
  absl::StatusOr<union ibv_gid> GetLocalGid() const;

  // Returns the Queue Pair Number (QPN).
  uint32_t Qpn() const { return qp_ != nullptr ? qp_->qp_num : 0; }

  // Returns the underlying ibv_qp handle.
  struct ibv_qp* GetQp() const { return qp_; }

  // Returns the dedicated completion queue for this Queue Pair.
  struct ibv_cq* GetSendCq() const { return cq_; }
  struct ibv_cq* GetRecvCq() const { return cq_; }
  struct ibv_cq* GetCq() const { return cq_; }

  // Returns the associated device context.
  RdmaDeviceContext* GetDeviceContext() const { return device_context_; }

 private:
  // Transport mode constant.
  static constexpr enum ibv_qp_type kQpType = IBV_QPT_RC;

  // Operational state of the Queue Pair.
  enum class State {
    kReset,
    kInit,
    kRtr,
    kRts,
    kError,
  };

  RdmaQueuePair(RdmaDeviceContext* device_context, struct ibv_qp* qp,
                struct ibv_cq* cq, const Options& options);

  // Internal state machine transitions.
  absl::Status Init();
  absl::Status Rtr(uint32_t remote_qpn, const union ibv_gid& remote_gid,
                   uint32_t remote_psn);
  absl::Status Rts(uint32_t local_psn);

 private:
  RdmaDeviceContext* const device_context_;
  struct ibv_qp* qp_;
  struct ibv_cq* cq_;
  const Options options_;
  State state_;
};

}  // namespace peregrine::internal

#endif  // PEREGRINE_SRC_INTERNAL_RDMA_RDMA_QUEUE_PAIR_H_
