#include "src/internal/rdma/rdma_acceptor.h"

#include <infiniband/verbs.h>

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <memory>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "absl/base/optimization.h"
#include "absl/log/check.h"
#include "absl/log/log.h"
#include "absl/memory/memory.h"
#include "absl/status/status.h"
#include "absl/strings/str_format.h"
#include "absl/synchronization/mutex.h"
#include "src/api/transport_types.h"
#include "src/internal/base/config.h"
#include "src/internal/base/endpoint.h"
#include "src/internal/base/hostinfo.h"
#include "src/internal/channel/channel.h"
#include "src/internal/channel/channel_util.h"
#include "src/internal/control/control.h"
#include "src/internal/control/message.pb.h"
#include "src/internal/control/message_internal.pb.h"
#include "src/internal/rdma/rdma_device_context.h"
#include "src/internal/rdma/rdma_device_manager.h"
#include "src/internal/rdma/rdma_memory_manager.h"
#include "src/internal/rdma/rdma_queue_pair.h"
#include "src/util/util.h"

namespace peregrine::internal {

std::unique_ptr<RdmaAcceptor> RdmaAcceptor::Create(const Config& config,
                                                   HostInfo& self,
                                                   Control& control) {
  if (config.transport_type != TransportType::kRdma) {
    return nullptr;
  }

  auto devmgr_or = RdmaDeviceManager::Create();
  if (!devmgr_or.ok()) {
    LOG(WARNING) << "failed to create RDMA device manager: "
                 << devmgr_or.status();
    return nullptr;
  }
  auto devmgr = std::move(*devmgr_or);

  for (const auto& dev : devmgr->Devices()) {
    self.rdma_interfaces.push_back({
        .name = std::string(dev->Name()),
        .gid = std::string(reinterpret_cast<const char*>(dev->LocalGid().raw),
                           sizeof(dev->LocalGid().raw)),
        .port_num = RdmaDeviceContext::kDefaultPortNum,
    });
  }
  if ABSL_PREDICT_FALSE (self.rdma_interfaces.empty()) {
    LOG(WARNING) << "no active RDMA devices found: " << self;
    return nullptr;
  }

  auto acceptor = absl::WrapUnique(
      new RdmaAcceptor(config, self, control, std::move(devmgr)));

  // Register RDMA connection handler into Control.
  control.SetRdmaConnectHandler(
      [a = acceptor.get()](const proto::RdmaConnectRequest& req,
                           proto::RdmaConnectResponse* resp) {
        return a->handleConnect(req, resp);
      });

  return acceptor;
}

RdmaAcceptor::RdmaAcceptor(const Config& config, const HostInfo& self,
                           Control& control,
                           std::unique_ptr<RdmaDeviceManager> rdma_devmgr)
    : config_(config),
      self_(self),
      control_(control),
      rdma_devmgr_(std::move(rdma_devmgr)) {
  DCHECK(rdma_devmgr_ != nullptr);
  rdma_memmgr_ = std::make_unique<RdmaMemoryManager>(rdma_devmgr_.get());
}

RdmaAcceptor::~RdmaAcceptor() { control_.SetRdmaConnectHandler(nullptr); }

uint32_t RdmaAcceptor::genPsn() {
  return util::Random<uint32_t>(bitgen_) & 0x00FF'FFFF;
}

absl::Status RdmaAcceptor::RegisterMemory(void* addr, size_t length) {
  absl::MutexLock _(mu_);
  if (rdma_memmgr_ != nullptr) {
    return rdma_memmgr_->RegisterMemory(addr, length);
  }
  return absl::FailedPreconditionError("RDMA memory manager not initialized");
}

absl::Status RdmaAcceptor::DeregisterMemory(const void* addr) {
  absl::MutexLock _(mu_);
  if (rdma_memmgr_ != nullptr) {
    return rdma_memmgr_->DeregisterMemory(addr);
  }
  return absl::FailedPreconditionError("RDMA memory manager not initialized");
}

std::vector<std::unique_ptr<Channel>> RdmaAcceptor::Connect(
    const Endpoint& peer, int num_conns) {
  std::vector<std::unique_ptr<Channel>> channels;
  if (rdma_devmgr_ == nullptr || rdma_devmgr_->Devices().empty()) {
    LOG(WARNING) << "no local RDMA devices available";
    return channels;
  }

  const auto& local_devices = rdma_devmgr_->Devices();
  auto peer_info = control_.GetPeerHostInfo(peer);
  if (!peer_info.ok()) {
    LOG(WARNING) << "failed to resolve peer " << peer << ": "
                 << peer_info.status();
    return channels;
  }
  const auto& remote_interfaces = peer_info->rdma_interfaces;
  if (remote_interfaces.empty()) {
    LOG(WARNING) << "no RDMA interfaces found for peer " << peer;
    return channels;
  }

  for (int i = 0; i < 2 * num_conns; ++i) {
    // TODO: Assumes interface indices are rail-aligned (i.e. local interface at
    // index 0 connects to remote interface at index 0 on the same rail). In
    // environments with physical rail isolation, cross-rail communication is
    // physically unsupported or blocked, causing connections to fail. In the
    // future, support dynamic topology discovery or explicit rail matching
    // rather than relying on positional index alignment.
    const size_t local_idx = i % local_devices.size();
    const size_t remote_idx = i % remote_interfaces.size();
    RdmaDeviceContext* local_dev = local_devices[local_idx].get();
    const std::string_view remote_device_name =
        remote_interfaces[remote_idx].name;

    auto qp_or = RdmaQueuePair::Create(local_dev);
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
      if (rdma_memmgr_ != nullptr) {
        local_lkey = rdma_memmgr_->GetDefaultLKey(local_dev->Name());
        initiator_rkey = rdma_memmgr_->GetDefaultRKey(local_dev->Name());
      }
    }

    auto resp_or = control_.ConnectRdmaPeer(peer, remote_device_name, qp->Qpn(),
                                            local_dev->LocalGid().raw,
                                            local_psn, initiator_rkey);
    if (!resp_or.ok()) {
      LOG(WARNING) << "ConnectRdmaPeer RPC failed for peer " << peer << ": "
                   << resp_or.status();
      continue;
    }
    const auto& resp = *resp_or;

    if (resp.gid().size() != sizeof(union ibv_gid)) {
      LOG(WARNING) << "invalid GID size in RdmaConnectResponse from peer "
                   << peer;
      continue;
    }

    union ibv_gid remote_gid = {};
    std::memcpy(remote_gid.raw, resp.gid().data(), sizeof(remote_gid.raw));

    auto status = qp->Connect(resp.qpn(), remote_gid, resp.psn(), local_psn);
    if (!status.ok()) {
      LOG(WARNING) << "failed to connect local QP to RTS: " << status;
      continue;
    }

    channels.push_back(
        CreateRdmaChannel(std::move(qp), local_lkey, resp.rkey()));
    if (channels.size() >= num_conns) break;
  }

  return channels;
}

absl::Status RdmaAcceptor::handleConnect(const proto::RdmaConnectRequest& req,
                                         proto::RdmaConnectResponse* resp) {
  if (rdma_devmgr_ == nullptr) {
    return absl::FailedPreconditionError("RDMA is not enabled on this host");
  }
  if (resp == nullptr) {
    return absl::InvalidArgumentError("null response pointer");
  }

  RdmaDeviceContext* dev = rdma_devmgr_->GetDevice(req.device_name());
  if (dev == nullptr) {
    return absl::NotFoundError(
        absl::StrFormat("RDMA device not found: %s", req.device_name()));
  }

  if (req.gid().size() != sizeof(union ibv_gid)) {
    return absl::InvalidArgumentError(
        absl::StrFormat("invalid GID size: expected %d, got %d",
                        sizeof(union ibv_gid), req.gid().size()));
  }

  auto qp_or = RdmaQueuePair::Create(dev);
  if (!qp_or.ok()) return qp_or.status();
  auto qp = std::move(*qp_or);

  union ibv_gid remote_gid = {};
  std::memcpy(remote_gid.raw, req.gid().data(), sizeof(remote_gid.raw));

  const uint32_t local_psn = genPsn();
  auto status = qp->Connect(req.qpn(), remote_gid, req.psn(), local_psn);
  if (!status.ok()) return status;

  resp->set_qpn(qp->Qpn());
  resp->set_gid(
      std::string_view(reinterpret_cast<const char*>(dev->LocalGid().raw),
                       sizeof(dev->LocalGid().raw)));
  resp->set_psn(local_psn);

  {
    absl::MutexLock _(mu_);
    if (rdma_memmgr_ != nullptr) {
      resp->set_rkey(rdma_memmgr_->GetDefaultRKey(dev->Name()));
    }
    inbound_rdma_qps_.push_back(std::move(qp));
  }

  return absl::OkStatus();
}

}  // namespace peregrine::internal
