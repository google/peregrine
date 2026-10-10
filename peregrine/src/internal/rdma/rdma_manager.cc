#include "peregrine/src/internal/rdma/rdma_manager.h"

#include <infiniband/verbs.h>

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <memory>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "absl/log/check.h"
#include "absl/log/log.h"
#include "absl/memory/memory.h"
#include "absl/status/status.h"
#include "absl/strings/str_format.h"
#include "absl/synchronization/mutex.h"
#include "peregrine/src/api/transport_types.h"
#include "peregrine/src/internal/base/config.h"
#include "peregrine/src/internal/base/endpoint.h"
#include "peregrine/src/internal/base/hostinfo.h"
#include "peregrine/src/internal/base/nicinfo.h"
#include "peregrine/src/internal/control/control.h"
#include "peregrine/src/internal/control/message.pb.h"
#include "peregrine/src/internal/control/message_internal.pb.h"
#include "peregrine/src/internal/rdma/rdma_conn.h"
#include "peregrine/src/internal/rdma/rdma_context.h"
#include "peregrine/src/internal/rdma/rdma_device.h"
#include "peregrine/src/internal/rdma/rdma_memory.h"
#include "peregrine/src/internal/rdma/rdma_qpair.h"
#include "peregrine/src/util/ipaddr.h"
#include "peregrine/src/util/nic.h"
#include "peregrine/src/util/util.h"

namespace peregrine::internal {

std::unique_ptr<RdmaManager> RdmaManager::Create(const Config& config,
                                                 HostInfo& self,
                                                 Control& control) {
  if (config.transport_type != TransportType::kRdma) {
    return nullptr;
  }

  auto device_or = RdmaDevice::Create();
  if (!device_or.ok()) {
    LOG(WARNING) << "failed to create RDMA device: " << device_or.status();
    return nullptr;
  }
  auto device = std::move(*device_or);

  std::vector<NicInfo> nics;
  nics.reserve(device->Contexts().size());
  for (const auto& ctx : device->Contexts()) {
    util::ipv6_t ipv6;
    std::memcpy(ipv6.s6_addr, ctx->LocalGid().raw, sizeof(ipv6));
    const NicInfo nic(std::string(ctx->Name()), util::NicType::kRDMA,
                      {Endpoint(ipv6, RdmaContext::kDefaultPort)});
    DCHECK(nic.IsValid());
    nics.push_back(nic);
  }
  if (nics.empty()) {
    LOG(WARNING) << "no active RDMA devices found: " << self;
    return nullptr;
  }
  self.data_plane_listeners.insert(self.data_plane_listeners.end(),
                                   nics.begin(), nics.end());

  auto mgr = absl::WrapUnique(
      new RdmaManager(config, self, control, std::move(device)));

  control.SetRdmaConnHandler([a = mgr.get()](const proto::RdmaConnReq& req,
                                             proto::RdmaConnResp* resp) {
    return a->handleConnect(req, resp);
  });

  return mgr;
}

RdmaManager::RdmaManager(const Config& config, const HostInfo& self,
                         Control& control, std::unique_ptr<RdmaDevice> device)
    : config_(config),
      self_(self),
      control_(control),
      device_(std::move(device)) {
  DCHECK(device_ != nullptr);
  memory_ = std::make_unique<RdmaMemory>(device_.get());
}

RdmaManager::~RdmaManager() { control_.SetRdmaConnHandler(nullptr); }

uint32_t RdmaManager::genPsn() {
  absl::MutexLock _(mu_);
  return util::Random<uint32_t>(bitgen_) & 0x00FF'FFFF;
}

absl::Status RdmaManager::RegisterMemory(void* addr, size_t length) {
  absl::MutexLock _(mu_);
  if (memory_ != nullptr) {
    return memory_->RegisterMemory(addr, length);
  }
  return absl::FailedPreconditionError("RDMA memory not initialized");
}

absl::Status RdmaManager::UnregisterMemory(const void* addr) {
  absl::MutexLock _(mu_);
  if (memory_ != nullptr) {
    return memory_->UnregisterMemory(addr);
  }
  return absl::FailedPreconditionError("RDMA memory manager not initialized");
}

std::vector<std::unique_ptr<RdmaConn>> RdmaManager::Connect(
    const Endpoint& peer_control, int num_conns) {
  std::vector<std::unique_ptr<RdmaConn>> conns_;
  if (device_ == nullptr || device_->Contexts().empty()) {
    LOG(WARNING) << "no local RDMA devices available";
    return conns_;
  }

  const auto peer_info = control_.GetPeerHostInfo(peer_control);
  if (!peer_info.ok()) {
    LOG(WARNING) << "failed to resolve peer " << peer_control << ": "
                 << peer_info.status();
    return conns_;
  }

  std::vector<NicInfo> remote_interfaces;
  for (const auto& nic : peer_info->data_plane_listeners) {
    if (nic.type == util::NicType::kRDMA) {
      remote_interfaces.push_back(nic);
    }
  }
  if (remote_interfaces.empty()) {
    LOG(WARNING) << "no RDMA interfaces found for peer " << peer_control;
    return conns_;
  }

  const auto& ctxs = device_->Contexts();
  for (int i = 0; i < 2 * num_conns; ++i) {
    // TODO: Assumes interface indices are rail-aligned (i.e. local interface at
    // index 0 connects to remote interface at index 0 on the same rail). In
    // environments with physical rail isolation, cross-rail communication is
    // physically unsupported or blocked, causing connections to fail. In the
    // future, support dynamic topology discovery or explicit rail matching
    // rather than relying on positional index alignment.
    const size_t local_idx = i % ctxs.size();
    const size_t remote_idx = i % remote_interfaces.size();
    RdmaContext* ctx = ctxs[local_idx].get();
    const std::string_view remote_device_name =
        remote_interfaces[remote_idx].name;

    auto qp_or = RdmaQPair::Create(ctx);
    if (!qp_or.ok()) {
      LOG(WARNING) << "failed to create local RDMA queue pair: "
                   << qp_or.status();
      continue;
    }
    auto qp = std::move(*qp_or);

    const uint32_t local_psn = genPsn();
    uint32_t local_lkey = 0;
    uint32_t initiator_rkey = 0;
    {
      absl::MutexLock _(mu_);
      if (memory_ != nullptr) {
        local_lkey = memory_->GetDefaultLKey(ctx->Name());
        initiator_rkey = memory_->GetDefaultRKey(ctx->Name());
      }
    }

    auto resp_or = control_.ConnectRdmaPeer(peer_control, remote_device_name,
                                            qp->Qpn(), ctx->LocalGid().raw,
                                            local_psn, initiator_rkey);
    if (!resp_or.ok()) {
      LOG(WARNING) << "ConnectRdmaPeer RPC failed for peer " << peer_control
                   << ": " << resp_or.status();
      continue;
    }
    const auto& resp = *resp_or;

    if (resp.gid().size() != sizeof(union ibv_gid)) {
      LOG(WARNING) << "invalid GID size in RdmaConnectResponse from peer "
                   << peer_control;
      continue;
    }

    union ibv_gid remote_gid = {};
    std::memcpy(remote_gid.raw, resp.gid().data(), sizeof(remote_gid.raw));

    auto status = qp->Connect(resp.qpn(), remote_gid, resp.psn(), local_psn);
    if (!status.ok()) {
      LOG(WARNING) << "failed to connect local QP to RTS: " << status;
      continue;
    }

    conns_.push_back(
        std::make_unique<RdmaConn>(std::move(qp), local_lkey, resp.rkey()));
    if (conns_.size() >= num_conns) break;
  }

  return conns_;
}

absl::Status RdmaManager::handleConnect(const proto::RdmaConnReq& req,
                                        proto::RdmaConnResp* resp) {
  if (device_ == nullptr) {
    return absl::FailedPreconditionError("RDMA is not enabled on this host");
  }
  if (req.gid().size() != sizeof(union ibv_gid)) {
    return absl::InvalidArgumentError(
        absl::StrFormat("invalid GID size: expected %d, got %d",
                        sizeof(union ibv_gid), req.gid().size()));
  }
  if (resp == nullptr) {
    return absl::InvalidArgumentError("null response pointer");
  }

  RdmaContext* ctx = device_->GetContext(req.device_name());
  if (ctx == nullptr) {
    return absl::NotFoundError(
        absl::StrFormat("RDMA device not found: %s", req.device_name()));
  }

  auto qp_or = RdmaQPair::Create(ctx);
  if (!qp_or.ok()) return qp_or.status();
  auto qp = std::move(*qp_or);

  union ibv_gid remote_gid = {};
  std::memcpy(remote_gid.raw, req.gid().data(), sizeof(remote_gid.raw));

  const uint32_t local_psn = genPsn();
  auto status = qp->Connect(req.qpn(), remote_gid, req.psn(), local_psn);
  if (!status.ok()) return status;

  const auto local_gid = ctx->LocalGid().raw;
  resp->set_qpn(qp->Qpn());
  resp->set_psn(local_psn);
  resp->set_gid(std::string_view(reinterpret_cast<const char*>(local_gid),
                                 sizeof(local_gid)));
  {
    absl::MutexLock _(mu_);
    if (memory_ != nullptr) {
      resp->set_rkey(memory_->GetDefaultRKey(ctx->Name()));
    }
    inbound_rdma_qps_.push_back(std::move(qp));
  }
  return absl::OkStatus();
}

}  // namespace peregrine::internal
