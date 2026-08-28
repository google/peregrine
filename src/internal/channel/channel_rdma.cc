#include "src/internal/channel/channel_rdma.h"

#include <infiniband/verbs.h>

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <limits>
#include <memory>
#include <string>
#include <string_view>
#include <utility>

#include "absl/log/check.h"
#include "absl/log/log.h"
#include "absl/strings/str_format.h"
#include "absl/types/span.h"
#include "src/internal/base/types.h"
#include "src/internal/chunk/chunk.h"
#include "src/internal/chunk/chunk_flatbuf.h"
#include "src/internal/rdma/rdma_queue_pair.h"

namespace peregrine::internal {

RdmaChannel::RdmaChannel(std::unique_ptr<RdmaQueuePair> qp, uint32_t lkey,
                         uint32_t rkey)
    : qp_(std::move(qp)), lkey_(lkey), rkey_(rkey), is_shutdown_(false) {
  DCHECK(qp_ != nullptr);
}

RdmaChannel::~RdmaChannel() = default;

void RdmaChannel::Shutdown() {
  if (is_shutdown_.exchange(true, std::memory_order_acq_rel)) {
    return;
  }
  if (qp_ != nullptr && qp_->GetQp() != nullptr) {
    // Transition QP to ERROR state to immediately abort and flush in-flight
    // hardware DMA transfers.
    struct ibv_qp_attr attr = {};
    attr.qp_state = IBV_QPS_ERR;
    ibv_modify_qp(qp_->GetQp(), &attr, IBV_QP_STATE);
  }
}

ssize_t RdmaChannel::ReadV(absl::Span<IoVec> /*iovecs*/) {
  // TODO: Implement ReadV for RDMA.
  LOG(ERROR) << "RdmaChannel::ReadV is not yet implemented";
  return -1;
}

ssize_t RdmaChannel::WriteV(absl::Span<const IoVec> iovecs) {
  if (is_shutdown_.load(std::memory_order_acquire)) return -1;
  if (qp_ == nullptr || qp_->GetQp() == nullptr) return -1;

  // One-sided RDMA requires exactly [ChunkHeader, Payload].
  if (iovecs.size() != 2) {
    LOG(ERROR)
        << "RdmaChannel::WriteV requires exactly 2 iovecs: [ChunkHeader, "
           "Payload]";
    return -1;
  }

  // 1. Deserialize ChunkHeader from iovecs[0] to extract destination address
  ChunkHeader chunk;
  const std::string_view header_str(
      static_cast<const char*>(iovecs[0].iov_base), iovecs[0].iov_len);
  if (!ChunkUtil::Deserialize(header_str, chunk) || !chunk.IsValid()) {
    LOG(ERROR) << "failed to deserialize valid chunk header in RdmaChannel";
    return -1;
  }

  const void* const payload_addr = iovecs[1].iov_base;
  const size_t payload_len = iovecs[1].iov_len;
  if (payload_len > std::numeric_limits<uint32_t>::max()) {
    LOG(ERROR) << "payload length " << payload_len << " exceeds uint32_t max";
    return -1;
  }
  const uint64_t remote_dest_addr = chunk.addr.value();

  struct ibv_sge sge = {};
  sge.addr = reinterpret_cast<uintptr_t>(payload_addr);
  sge.length = static_cast<uint32_t>(payload_len);
  sge.lkey = lkey_;

  struct ibv_send_wr wr = {};
  wr.wr_id = reinterpret_cast<uint64_t>(this);
  wr.sg_list = &sge;
  wr.num_sge = (payload_len > 0) ? 1 : 0;
  wr.opcode = IBV_WR_RDMA_WRITE;
  wr.send_flags = IBV_SEND_SIGNALED;
  wr.wr.rdma.remote_addr = remote_dest_addr;
  wr.wr.rdma.rkey = rkey_;

  struct ibv_send_wr* bad_wr = nullptr;
  if (const int ret = ibv_post_send(qp_->GetQp(), &wr, &bad_wr); ret != 0) {
    LOG(ERROR) << "ibv_post_send(IBV_WR_RDMA_WRITE) failed: " << strerror(ret);
    return -1;
  }

  // Poll dedicated send CQ for completion
  struct ibv_cq* const cq = qp_->GetSendCq();
  if (cq == nullptr) return -1;

  struct ibv_wc wc = {};
  int poll_ret = 0;
  do {
    poll_ret = ibv_poll_cq(cq, 1, &wc);
    if (poll_ret < 0) {
      LOG(ERROR) << "ibv_poll_cq failed on send completion";
      return -1;
    }
  } while (poll_ret == 0 && !is_shutdown_.load(std::memory_order_relaxed));

  if (is_shutdown_.load(std::memory_order_acquire)) return -1;

  if (wc.status != IBV_WC_SUCCESS) {
    LOG(ERROR) << "RDMA send completion failed with status: "
               << ibv_wc_status_str(wc.status) << " (" << wc.status << ")";
    return -1;
  }

  return iovecs[0].iov_len + payload_len;
}

std::string RdmaChannel::ToString() const {
  return absl::StrFormat("RdmaChannel: qpn=%u, lkey=%u, rkey=%u",
                         qp_ != nullptr ? qp_->Qpn() : 0, lkey_, rkey_);
}

}  // namespace peregrine::internal
