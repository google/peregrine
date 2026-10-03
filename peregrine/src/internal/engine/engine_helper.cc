#include "peregrine/src/internal/engine/engine_helper.h"

#include <cstddef>
#include <cstdint>
#include <memory>
#include <utility>
#include <vector>

#include "absl/base/optimization.h"
#include "absl/log/check.h"
#include "absl/log/log.h"
#include "absl/memory/memory.h"
#include "absl/status/status.h"
#include "absl/types/span.h"
#include "peregrine/src/api/transport_types.h"
#include "peregrine/src/internal/assumptions.h"
#include "peregrine/src/internal/base/config.h"
#include "peregrine/src/internal/base/endpoint.h"
#include "peregrine/src/internal/base/hostinfo.h"
#include "peregrine/src/internal/base/nicinfo.h"
#include "peregrine/src/internal/channel/channel.h"
#include "peregrine/src/internal/channel/channel_util.h"
#include "peregrine/src/internal/control/control.h"
#include "peregrine/src/internal/metrics/engine_metrics.h"
#include "peregrine/src/internal/rdma/rdma_acceptor.h"
#include "peregrine/src/internal/socket/socket_tcp.h"
#include "peregrine/src/internal/socket/socket_util.h"
#include "peregrine/src/internal/socket/tcp_manager.h"
#include "peregrine/src/util/nic.h"
#include "peregrine/src/util/thread.h"

namespace peregrine::internal {

std::unique_ptr<EngineHelper> EngineHelper::Create(const Config& config,
                                                   HostInfo& self,
                                                   Control& control) {
  static_assert(assumptions::kHostInfoDependsOnControlAndDataPlanes);
  DCHECK(config.IsValid());

  self.data_plane_listeners.clear();
  auto rdma_acceptor = RdmaAcceptor::Create(config, self, control);
  if (rdma_acceptor == nullptr) {
    if (config.transport_type == TransportType::kRdma) {
      LOG(ERROR) << "failed to create rdma acceptor for " << self;
      return nullptr;
    }
    LOG(WARNING) << "failed to create rdma acceptor for " << self
                 << ", continue with TCP but no RDMA";
  }

  auto tcp_mgr = TcpManager::Create(self);
  if ABSL_PREDICT_FALSE (tcp_mgr == nullptr) {
    LOG(ERROR) << "failed to create tcp manager for " << self;
    return nullptr;
  }
  if ABSL_PREDICT_FALSE (!self.IsValid()) {
    LOG(ERROR) << "invalid self host info for " << self;
    return nullptr;
  }

  DCHECK(!config.require_dataplane_encryption);
  return absl::WrapUnique(new EngineHelper(
      config, self, control, std::move(tcp_mgr), std::move(rdma_acceptor)));
}

EngineHelper::EngineHelper(const Config& config, const HostInfo& self,
                           Control& control,
                           std::unique_ptr<TcpManager> tcp_mgr,
                           std::unique_ptr<RdmaAcceptor> rdma_acceptor)
    : config_(config),
      self_(self),
      control_(control),
      tcp_mgr_(std::move(tcp_mgr)),
      rdma_acceptor_(std::move(rdma_acceptor)) {
  DCHECK(invariant());
  tcpmgr_thread_ = util::Jthread([this]() {
    constexpr bool kGenBlocking = true;
    tcp_mgr_->Start(kGenBlocking);
  });
  LOG(INFO) << "engine helper created @ " << self_;
}

EngineHelper::~EngineHelper() {
  DCHECK(invariant());
  tcp_mgr_->Stop();
  LOG(INFO) << "engine helper destroyed @ " << self_;
}

EngineHelper::Channels EngineHelper::GetAcceptedChannels() {
  Channels chs;
  for (auto& socket : tcp_mgr_->GetIncomingSockets()) {
    const Endpoint peer_target = PeerEndpoint(socket->fd());
    if ABSL_PREDICT_FALSE (!peer_target.HasNonzeroIpPort()) {
      LOG(WARNING) << "invalid peer endpoint for " << *socket;
    } else {
      auto ch = CreateTcpChannel(std::move(socket));
      chs.push_back(std::move(ch));
    }
  }
  return chs;
}

EngineHelper::Channels EngineHelper::Connect(const Endpoint& peer_control) {
  const int n = config_.num_conns_per_peer;
  const bool blocking = true;
  if (config_.transport_type == TransportType::kRdma) {
    return connectRdma(peer_control, n);
  } else {
    return connectTcp(peer_control, n, blocking);
  }
}

namespace {
std::vector<Endpoint> GetTcpEndpoints(Control& control,
                                      const Endpoint& peer_control) {
  const auto peer_info = control.GetPeerHostInfo(peer_control);
  if (!peer_info.ok()) {
    LOG(WARNING) << "failed to get host info for peer " << peer_control << ": "
                 << peer_info.status();
    return {};
  }
  std::vector<Endpoint> endpoints;
  for (const auto& nic : peer_info->data_plane_listeners) {
    if (nic.type == util::NicType::kIP) {
      endpoints.insert(endpoints.end(), nic.endpoints.begin(),
                       nic.endpoints.end());
    }
  }
  if (endpoints.empty()) {
    LOG(WARNING) << "no tcp listener found for peer " << peer_control;
  }
  return endpoints;
}
}  // namespace

EngineHelper::Channels EngineHelper::getConnectedTcpChannels(
    const absl::Span<const Endpoint> peers) {
  static_assert(assumptions::kAllConnectedTcpSocketsAreStillBlocking);
  Channels chs;
  uint64_t failures = 0;
  for (const Endpoint& peer : peers) {
    for (auto& socket : tcp_mgr_->GetOutgoingSockets(peer)) {
      DCHECK_NE(socket, nullptr);
      if (socket->IsNonBlocking() && socket->SetBlocking() < 0) {
        LOG(WARNING) << "failed to set blocking mode for " << *socket;
        ++failures;
        continue;
      }
      std::unique_ptr<Channel> ch = CreateTcpChannel(std::move(socket));
      chs.push_back(std::move(ch));
    }
  }
  if (failures > 0) metrics_.tcp_connect_failures.Add(failures);
  return chs;
}

int EngineHelper::numPendingTcpConnections(
    const absl::Span<const Endpoint> peers) const {
  int pending = 0;
  for (const Endpoint& peer : peers) {
    pending += tcp_mgr_->NumPendingOutgoing(peer);
  }
  return pending;
}

int EngineHelper::NumPendingConnections(const Endpoint& peer_control) const {
  const std::vector<Endpoint> peers = GetTcpEndpoints(control_, peer_control);
  return numPendingTcpConnections(peers);
}

EngineHelper::Channels EngineHelper::connectTcp(const Endpoint& peer_control,
                                                const int n,
                                                const bool blocking) {
  const std::vector<Endpoint> peers = GetTcpEndpoints(control_, peer_control);
  if (peers.empty()) return {};

  static_assert(assumptions::kAllConnectedTcpSocketsAreStillBlocking);
  // TODO(yongx): build connection locality group
  const Endpoint self = {};
  const Endpoint& peer = peers[0];
  uint64_t failures = tcp_mgr_->GetAndResetOutgoingFailures();
  const int to_connect = n - numPendingTcpConnections(peers);
  for (int i = 0, count = 0; count < to_connect && i < 2 * to_connect; ++i) {
    DCHECK(!config_.require_dataplane_encryption);
    if (tcp_mgr_->Connect(self, peer, blocking) < 0) {
      ++failures;
    } else {
      ++count;
    }
  }
  if (failures > 0) metrics_.tcp_connect_failures.Add(failures);
  return getConnectedTcpChannels(peers);
}

EngineHelper::Channels EngineHelper::connectRdma(const Endpoint& peer_control,
                                                 const int n) {
  if (rdma_acceptor_ == nullptr) return {};
  return rdma_acceptor_->Connect(peer_control, n);
}

absl::Status EngineHelper::checkRdmaAcceptor() const {
  DCHECK_EQ(rdma_acceptor_, nullptr);
  return config_.transport_type == TransportType::kRdma
             ? absl::FailedPreconditionError("null rdma acceptor")
             : absl::OkStatus();
}

absl::Status EngineHelper::RegisterMemory(void* addr, size_t length) {
  if (rdma_acceptor_ != nullptr) {
    return rdma_acceptor_->RegisterMemory(addr, length);
  }
  return checkRdmaAcceptor();
}

absl::Status EngineHelper::UnregisterMemory(const void* addr) {
  if (rdma_acceptor_ != nullptr) {
    return rdma_acceptor_->UnregisterMemory(addr);
  }
  return checkRdmaAcceptor();
}

}  // namespace peregrine::internal
