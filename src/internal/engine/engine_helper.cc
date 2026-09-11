#include "src/internal/engine/engine_helper.h"

#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <thread>  // NOLINT
#include <utility>

#include "absl/base/optimization.h"
#include "absl/log/check.h"
#include "absl/log/log.h"
#include "absl/memory/memory.h"
#include "absl/status/status.h"
#include "absl/synchronization/mutex.h"
#include "src/api/transport_types.h"
#include "src/internal/assumptions.h"
#include "src/internal/base/config.h"
#include "src/internal/base/endpoint.h"
#include "src/internal/base/hostinfo.h"
#include "src/internal/channel/channel.h"
#include "src/internal/channel/channel_util.h"
#include "src/internal/control/control.h"
#include "src/internal/metrics/engine_metrics.h"
#include "src/internal/rdma/rdma_acceptor.h"
#include "src/internal/socket/acceptor.h"
#include "src/internal/socket/connector.h"
#include "src/internal/socket/socket_tcp.h"
#include "src/internal/socket/socket_util.h"

namespace peregrine::internal {

std::unique_ptr<EngineHelper> EngineHelper::Create(const Config& config,
                                                   HostInfo& self,
                                                   Control& control) {
  static_assert(assumptions::kHostInfoDependsOnControlAndDataPlanes);
  DCHECK(config.IsValid());

  auto rdma_acceptor = RdmaAcceptor::Create(config, self, control);
  if (rdma_acceptor == nullptr) {
    LOG(WARNING) << "failed to create rdma acceptor for " << self;
    LOG(WARNING) << "continue with TCP but no RDMA";
  }

  auto tcp_acceptor = TcpAcceptor::Create(self);
  if ABSL_PREDICT_FALSE (tcp_acceptor == nullptr) {
    LOG(ERROR) << "failed to create tcp acceptor for " << self;
    return nullptr;
  }
  if ABSL_PREDICT_FALSE (!self.IsValid()) {
    LOG(ERROR) << "invalid self host info for " << self;
    return nullptr;
  }

  DCHECK(!config.require_dataplane_encryption);
  return absl::WrapUnique(new EngineHelper(config, self, control,
                                           std::move(tcp_acceptor),
                                           std::move(rdma_acceptor)));
}

EngineHelper::EngineHelper(const Config& config, const HostInfo& self,
                           Control& control,
                           std::unique_ptr<TcpAcceptor> tcp_acceptor,
                           std::unique_ptr<RdmaAcceptor> rdma_acceptor)
    : config_(config),
      self_(self),
      control_(control),
      tcp_acceptor_(std::move(tcp_acceptor)),
      rdma_acceptor_(std::move(rdma_acceptor)) {
  DCHECK(invariant());
  tcp_acceptor_thread_ = std::jthread([this]() {
    tcp_acceptor_->Start([this](std::unique_ptr<TcpSocket> socket) {
      accept(std::move(socket));
    });
  });
  LOG(INFO) << "engine helper created @ " << self_;
}

EngineHelper::~EngineHelper() {
  DCHECK(invariant());
  tcp_acceptor_->Stop();
  LOG(INFO) << "engine helper destroyed @ " << self_;
}

void EngineHelper::accept(std::unique_ptr<TcpSocket> socket) {
  DCHECK_NE(socket, nullptr);

  const Endpoint peer_target = PeerEndpoint(socket->fd());
  if ABSL_PREDICT_FALSE (!peer_target.HasNonzeroIpPort()) {
    LOG(WARNING) << "invalid peer endpoint for " << *socket;
  } else {
    std::unique_ptr<Channel> ch = CreateTcpChannel(std::move(socket));
    addChannel(peer_target, std::move(ch));
  }
}

int EngineHelper::addChannel(const Endpoint& peer,
                             std::unique_ptr<Channel> channel) {
  absl::MutexLock _(channels_mu_);
  EngineHelper::Channels& chs = channels_[peer];
  chs.push_back(std::move(channel));
  return chs.size();
}

void EngineHelper::Connect(const Endpoint& peer) {
  const int n = config_.num_conns_per_peer;
  {
    absl::MutexLock _(channels_mu_);
    if (channels_[peer].size() >= n) return;
  }
  if (config_.transport_type == TransportType::kTcp) {
    connectTcp(peer);
  } else if (config_.transport_type == TransportType::kRdma) {
    connectRdma(peer);
  }
}

void EngineHelper::connectTcp(const Endpoint& peer) {
  DCHECK(!config_.require_dataplane_encryption);

  const auto peer_info = control_.GetPeerHostInfo(peer);
  if (!peer_info.ok()) {
    LOG(WARNING) << "failed to get host info for peer " << peer << ": "
                 << peer_info.status();
    return;
  }
  if (peer_info->data_plane_listeners.empty()) {
    LOG(WARNING) << "no data plane tcp listeners found for peer " << peer;
    return;
  }

  // TODO(yongx): connect to all data plane listeners of the peer.
  const Endpoint& peer_target = peer_info->data_plane_listeners[0];
  const int n = config_.num_conns_per_peer;
  uint64_t failures = 0;
  for (int i = 0; i < 2 * n; ++i) {
    std::unique_ptr<TcpSocket> socket = TcpConnector::Create(peer_target);
    if ABSL_PREDICT_FALSE (socket == nullptr) {
      ++failures;
      continue;
    }
    std::unique_ptr<Channel> ch = CreateTcpChannel(std::move(socket));
    if (addChannel(peer, std::move(ch)) >= n) break;
  }
  if (failures > 0) metrics_.tcp_connect_failures.Add(failures);
}

void EngineHelper::connectRdma(const Endpoint& peer) {
  if (rdma_acceptor_ == nullptr) {
    LOG(WARNING) << "no local RDMA acceptor available";
    return;
  }
  const int n = config_.num_conns_per_peer;
  for (auto& ch : rdma_acceptor_->Connect(peer, n)) {
    addChannel(peer, std::move(ch));
  }
}

absl::Status EngineHelper::RegisterMemory(void* addr, size_t length) {
  if (rdma_acceptor_ != nullptr) {
    return rdma_acceptor_->RegisterMemory(addr, length);
  }
  if (config_.transport_type == TransportType::kTcp) {
    return absl::OkStatus();
  }
  return absl::FailedPreconditionError("null RDMA acceptor");
}

absl::Status EngineHelper::UnregisterMemory(const void* addr) {
  if (rdma_acceptor_ != nullptr) {
    return rdma_acceptor_->UnregisterMemory(addr);
  }
  if (config_.transport_type == TransportType::kTcp) {
    return absl::OkStatus();
  }
  return absl::FailedPreconditionError("null RDMA acceptor");
}

}  // namespace peregrine::internal
