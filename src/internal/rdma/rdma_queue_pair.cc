#include "src/internal/rdma/rdma_queue_pair.h"

#include <infiniband/verbs.h>

#include <cerrno>
#include <cstdint>
#include <cstring>
#include <memory>

#include "absl/log/check.h"
#include "absl/log/log.h"
#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/strings/str_format.h"
#include "src/internal/rdma/rdma_device_context.h"

namespace peregrine::internal {

RdmaQueuePair::RdmaQueuePair(RdmaDeviceContext* device_context,
                             struct ibv_qp* qp, struct ibv_cq* cq,
                             const Options& options)
    : device_context_(device_context),
      qp_(qp),
      cq_(cq),
      options_(options),
      state_(State::kReset) {
  DCHECK(device_context_ != nullptr);
  DCHECK(qp_ != nullptr);
  DCHECK(cq_ != nullptr);
}

RdmaQueuePair::~RdmaQueuePair() {
  if (qp_ != nullptr) {
    const int ret = ibv_destroy_qp(qp_);
    if (ret != 0) {
      LOG(WARNING) << "failed to destroy ibv_qp: " << ret << " ("
                   << strerror(ret) << ")";
    }
    qp_ = nullptr;
  }
  if (cq_ != nullptr) {
    const int ret = ibv_destroy_cq(cq_);
    if (ret != 0) {
      LOG(WARNING) << "failed to destroy ibv_cq: " << ret << " ("
                   << strerror(ret) << ")";
    }
    cq_ = nullptr;
  }
}

absl::StatusOr<std::unique_ptr<RdmaQueuePair>> RdmaQueuePair::Create(
    RdmaDeviceContext* device_context, const Options& options) {
  if (device_context == nullptr) {
    return absl::InvalidArgumentError("device_context is null");
  }

  struct ibv_cq* cq = ibv_create_cq(device_context->GetDeviceContext(),
                                    options.cq_size, nullptr, nullptr, 0);
  if (cq == nullptr) {
    return absl::InternalError(
        absl::StrFormat("ibv_create_cq failed on device %s: %s",
                        device_context->Name(), strerror(errno)));
  }

  struct ibv_qp_init_attr init_attr = {};
  init_attr.qp_type = kQpType;
  init_attr.send_cq = cq;
  init_attr.recv_cq = cq;
  init_attr.cap.max_send_wr = options.max_send_wr;
  init_attr.cap.max_recv_wr = options.max_recv_wr;
  init_attr.cap.max_send_sge = options.max_send_sge;
  init_attr.cap.max_recv_sge = options.max_recv_sge;
  init_attr.sq_sig_all = options.sq_sig_all ? 1 : 0;

  struct ibv_qp* qp = ibv_create_qp(device_context->GetPd(), &init_attr);
  if (qp == nullptr) {
    ibv_destroy_cq(cq);
    return absl::InternalError(
        absl::StrFormat("ibv_create_qp failed on device %s: %s",
                        device_context->Name(), strerror(errno)));
  }

  std::unique_ptr<RdmaQueuePair> queue_pair(
      new RdmaQueuePair(device_context, qp, cq, options));
  absl::Status init_status = queue_pair->Init();
  if (!init_status.ok()) {
    return init_status;
  }
  return queue_pair;
}

absl::Status RdmaQueuePair::Init() {
  if (qp_ == nullptr) {
    return absl::FailedPreconditionError("ibv_qp is null");
  }

  struct ibv_qp_attr attr = {};
  attr.qp_state = IBV_QPS_INIT;
  attr.pkey_index = 0;
  attr.port_num = RdmaDeviceContext::kDefaultPortNum;
  attr.qp_access_flags = options_.access_flags;

  const int flags =
      IBV_QP_STATE | IBV_QP_PKEY_INDEX | IBV_QP_PORT | IBV_QP_ACCESS_FLAGS;
  const int ret = ibv_modify_qp(qp_, &attr, flags);
  if (ret != 0) {
    state_ = State::kError;
    return absl::InternalError(absl::StrFormat(
        "ibv_modify_qp(INIT) failed: %d (%s)", ret, strerror(ret)));
  }

  state_ = State::kInit;
  return absl::OkStatus();
}

absl::Status RdmaQueuePair::Rtr(uint32_t remote_qpn,
                                const union ibv_gid& remote_gid,
                                uint32_t remote_psn) {
  if (qp_ == nullptr) {
    return absl::FailedPreconditionError("ibv_qp is null");
  }
  if (state_ != State::kInit) {
    return absl::FailedPreconditionError(
        "QP must be in INIT state before transitioning to RTR");
  }

  struct ibv_qp_attr attr = {};
  attr.qp_state = IBV_QPS_RTR;
  attr.path_mtu = options_.path_mtu;
  attr.dest_qp_num = remote_qpn;
  attr.rq_psn = remote_psn;
  attr.max_dest_rd_atomic = options_.max_dest_rd_atomic;
  attr.min_rnr_timer = options_.min_rnr_timer;

  // Address Vector (AV) configuration for RoCEv2 GRH
  attr.ah_attr.is_global = 1;
  attr.ah_attr.port_num = RdmaDeviceContext::kDefaultPortNum;
  attr.ah_attr.sl = options_.sl;
  attr.ah_attr.src_path_bits = 0;
  attr.ah_attr.grh.dgid = remote_gid;
  attr.ah_attr.grh.sgid_index = device_context_->GidIndex();
  attr.ah_attr.grh.hop_limit = options_.hop_limit;
  attr.ah_attr.grh.traffic_class = options_.traffic_class;

  const int flags = IBV_QP_STATE | IBV_QP_AV | IBV_QP_PATH_MTU |
                    IBV_QP_DEST_QPN | IBV_QP_RQ_PSN |
                    IBV_QP_MAX_DEST_RD_ATOMIC | IBV_QP_MIN_RNR_TIMER;
  const int ret = ibv_modify_qp(qp_, &attr, flags);
  if (ret != 0) {
    state_ = State::kError;
    return absl::InternalError(absl::StrFormat(
        "ibv_modify_qp(RTR) failed: %d (%s)", ret, strerror(ret)));
  }

  state_ = State::kRtr;
  return absl::OkStatus();
}

absl::Status RdmaQueuePair::Rts(uint32_t local_psn) {
  if (qp_ == nullptr) {
    return absl::FailedPreconditionError("ibv_qp is null");
  }
  if (state_ != State::kRtr) {
    return absl::FailedPreconditionError(
        "QP must be in RTR state before transitioning to RTS");
  }

  struct ibv_qp_attr attr = {};
  attr.qp_state = IBV_QPS_RTS;
  attr.sq_psn = local_psn;
  attr.timeout = options_.timeout;
  attr.retry_cnt = options_.retry_cnt;
  attr.rnr_retry = options_.rnr_retry;
  attr.max_rd_atomic = options_.max_rd_atomic;

  const int flags = IBV_QP_STATE | IBV_QP_SQ_PSN | IBV_QP_TIMEOUT |
                    IBV_QP_RETRY_CNT | IBV_QP_RNR_RETRY |
                    IBV_QP_MAX_QP_RD_ATOMIC;
  const int ret = ibv_modify_qp(qp_, &attr, flags);
  if (ret != 0) {
    state_ = State::kError;
    return absl::InternalError(absl::StrFormat(
        "ibv_modify_qp(RTS) failed: %d (%s)", ret, strerror(ret)));
  }

  state_ = State::kRts;
  return absl::OkStatus();
}

absl::Status RdmaQueuePair::Connect(uint32_t remote_qpn,
                                    const union ibv_gid& remote_gid,
                                    uint32_t remote_psn, uint32_t local_psn) {
  absl::Status status = Rtr(remote_qpn, remote_gid, remote_psn);
  if (!status.ok()) return status;
  return Rts(local_psn);
}

absl::StatusOr<union ibv_gid> RdmaQueuePair::GetLocalGid() const {
  if (device_context_ == nullptr) {
    return absl::FailedPreconditionError("invalid device context");
  }
  return device_context_->LocalGid();
}

}  // namespace peregrine::internal
