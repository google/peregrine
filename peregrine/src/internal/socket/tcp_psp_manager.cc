#include "peregrine/src/internal/socket/tcp_psp_manager.h"

#include <sys/epoll.h>

#include <algorithm>
#include <atomic>
#include <cstdint>
#include <memory>
#include <utility>
#include <vector>

#include "absl/base/optimization.h"
#include "absl/container/flat_hash_map.h"
#include "absl/log/check.h"
#include "absl/log/log.h"
#include "absl/memory/memory.h"
#include "absl/status/status.h"
#include "absl/strings/str_cat.h"
#include "absl/synchronization/mutex.h"
#include "absl/time/clock.h"
#include "absl/time/time.h"
#include "peregrine/src/internal/assumptions.h"
#include "peregrine/src/internal/base/endpoint.h"
#include "peregrine/src/internal/base/hostinfo.h"
#include "peregrine/src/internal/base/nicinfo.h"
#include "peregrine/src/internal/base/types.h"
#include "peregrine/src/internal/event/poller.h"
#include "peregrine/src/internal/socket/psp/psp.h"
#include "peregrine/src/internal/socket/psp/psp_util.h"
#include "peregrine/src/internal/socket/socket_error.h"
#include "peregrine/src/internal/socket/socket_tcp.h"
#include "peregrine/src/internal/socket/socket_util.h"

namespace peregrine::internal {

std::unique_ptr<TcpSocket> PspTcpManager::createListener(Endpoint& endpoint,
                                                         Poller& poller) {
  static_assert(assumptions::kTcpListeningSocketsAreNonBlocking);
  DCHECK(!endpoint.HasZeroIpAddr());

  const int family = endpoint.GetIpAddr().AddressFamily();
  auto socket = TcpSocket::Create(family, /*blocking=*/false);
  if ABSL_PREDICT_FALSE (socket == nullptr) {
    return nullptr;
  }
  DCHECK(socket->IsNonBlocking());
  if ABSL_PREDICT_FALSE (socket->Listen(endpoint) < 0) {
    return nullptr;
  }
  const Endpoint e = SelfEndpoint(socket->fd());
  if (!e.HasNonzeroIpPort()) {
    return nullptr;
  }
  constexpr uint32_t kEvents = EPOLLIN | EPOLLERR | EPOLLRDHUP | EPOLLET;
  if ABSL_PREDICT_FALSE (poller.Register(socket->fd(), kEvents) < 0) {
    return nullptr;
  }
  endpoint = e;
  LOG(INFO) << "created, " << *socket;
  return socket;
}

std::unique_ptr<PspTcpManager> PspTcpManager::Create(HostInfo& self) {
  std::unique_ptr<Poller> poller = Poller::Create();
  if ABSL_PREDICT_FALSE (poller == nullptr) {
    LOG(ERROR) << "failed to create poller";
    return nullptr;
  }
  const bool loopback = self.control_plane_listener.GetIpAddr().IsLoopback();
  auto candidates = NicInfo::GetTcpListenerCandidates(loopback);
  if ABSL_PREDICT_FALSE (candidates.empty()) {
    LOG(ERROR) << "failed to find data plane tcp listener candidates";
    return nullptr;
  }
  HostInfo host = self;  // only update `self` on success
  absl::flat_hash_map<fd_t, Listener> listeners;
  for (auto& [_, ni] : candidates) {
    NicInfo nic(ni.name, ni.type, {});
    for (auto& e : ni.endpoints) {  // `e` will be modified below
      if (auto socket = createListener(e, *poller); socket != nullptr) {
        const fd_t fd = socket->fd();
        DCHECK(e.HasNonzeroIpPort());
        DCHECK_EQ(e, SelfEndpoint(fd));
        listeners.emplace(fd, Listener{std::move(socket), e});
        nic.endpoints.push_back(e);
      }
    }
    if (nic.IsValid()) host.data_plane_listeners.push_back(nic);
  }
  if ABSL_PREDICT_FALSE (listeners.empty()) {
    LOG(ERROR) << "failed to create data plane tcp listening sockets";
    return nullptr;
  }
  if ABSL_PREDICT_FALSE (!host.IsValid()) {
    LOG(ERROR) << "invalid self host info " << host;
    return nullptr;
  }
  self = host;
  auto m = new PspTcpManager(self, std::move(poller), std::move(listeners));
  return absl::WrapUnique(m);
}

int PspTcpManager::Connect(const Endpoint& self, const Endpoint& peer,
                           const bool blocking) {
  static_assert(assumptions::kTcpConnectingSocketsCanBeBlockingOrNonBlocking);
  DCHECK(peer.HasNonzeroIpPort());

  // TODO(yongx): non-blocking connect
  if (!blocking) return kConnectError;

  const int family = peer.GetIpAddr().AddressFamily();
  auto socket = TcpSocket::Create(family, /*blocking=*/true);
  if ABSL_PREDICT_FALSE (socket == nullptr) {
    return kConnectError;
  }
  if (!self.HasZeroIpAddr() && socket->Bind(self)) {
    return kConnectError;
  }
  DCHECK(socket->IsBlocking());
  if (socket->Connect(peer)) {
    return kConnectError;
  }
  DCHECK(socket->IsConnected());
  LOG(INFO) << "made " << *socket;
  AddConnected(std::move(socket));
  return kConnectSuccess;
}

void PspTcpManager::Start(OnAccept on_accept, bool gen_blocking) {
  static_assert(assumptions::kTcpListeningSocketsAreNonBlocking);
  DCHECK(invariant());
  LOG(INFO) << "starting, " << self_;

  constexpr int kTimeoutMs = 50;
  constexpr int kMaxEvents = 64;
  epoll_event events[kMaxEvents];
  while (!isStopped()) {
    const int nfds = poller_->BlockingWait(events, kMaxEvents, kTimeoutMs);
    if (isStopped()) break;
    if ABSL_PREDICT_FALSE (nfds < 0) {
      LOG_EVERY_N_SEC(ERROR, 1) << "poller wait failed";
      continue;
    }
    for (int i = 0; i < nfds; ++i) {
      const fd_t fd(events[i].data.fd);
      const auto it = listeners_.find(fd);
      if ABSL_PREDICT_FALSE (it == listeners_.end()) {
        LOG(WARNING) << "invalid fd " << fd;
        continue;
      }
      const TcpSocket* listener = it->second.socket.get();
      const int family = listener->family();
      while (true) {
        const int ret = listener->Accept(gen_blocking);
        if ABSL_PREDICT_FALSE (ret < 0) {
          if ABSL_PREDICT_FALSE (IsOutOfResource(ret)) {
            LOG_EVERY_N_SEC(ERROR, 3) << "out of resource, " << *listener;
            absl::SleepFor(absl::Milliseconds(50));  // avoid busy-looping
          } else if (IsWouldBlock(ret) || IsShutdown(ret)) {
            // do nothing
          } else {
            LOG(WARNING) << "accept failed, " << *listener;
          }
          break;
        }
        const fd_t new_fd(ret);
        auto socket = TcpSocket::Create(new_fd, family, gen_blocking);
        DCHECK(socket->MatchesBlocking());
        DCHECK(socket->IsConnected());
        LOG(INFO) << "made " << *socket;
        on_accept(std::move(socket));
      }
    }
  }
}

void PspTcpManager::Stop() {
  DCHECK(invariant());
  stop_.store(true, std::memory_order_relaxed);
  for (const auto& [fd, listener] : listeners_) {
    listener.socket->Shutdown();  // unblocks BlockingWait() above
  }
  LOG(INFO) << "stopped, " << self_;
}

bool PspTcpManager::invariant() const {
  const bool nonblocking_listeners =
      std::all_of(listeners_.begin(), listeners_.end(), [](const auto& pair) {
        const TcpSocket* const socket = pair.second.socket.get();
        return socket != nullptr && pair.first == socket->fd() &&
               socket->IsNonBlocking();
      });
  return self_.IsValid() && poller_ != nullptr && nonblocking_listeners;
}

std::unique_ptr<TcpSocket> PspTcpManager::ConnectPsp(
    const Endpoint& self, const Endpoint& peer, const Endpoint& peer_control,
    PspTokenExchange& psp_token_xchg) {
  DCHECK(peer.HasNonzeroIpPort());
  DCHECK(peer_control.HasNonzeroIpPort());
  DCHECK_NE(psp_token_xchg, nullptr);

  const int family = peer.GetIpAddr().AddressFamily();
  auto socket = TcpSocket::Create(family, /*blocking=*/true);
  if ABSL_PREDICT_FALSE (socket == nullptr) {
    return nullptr;
  }
  if (!self.HasZeroIpAddr() && socket->Bind(self)) {
    return nullptr;
  }
  const fd_t fd = socket->fd();
  const absl::StatusOr<PspToken> self_token = psp::AcquireRxSpiAndKey(fd);
  if (!self_token.ok() || !self_token->IsValid()) {
    LOG(WARNING) << "failed to acquire self rx psp token";
    return nullptr;
  }
  const absl::StatusOr<PspToken> peer_token =
      psp_token_xchg(self_token.value(), peer, peer_control);
  if (!peer_token.ok()) {
    LOG(WARNING) << "failed to exchange psp token with peer";
    return nullptr;
  }
  const absl::Status status = psp::SetTxSpiAndKey(fd, peer_token.value());
  if (!status.ok()) {
    LOG(WARNING) << "failed to set tx psp token: " << status;
    return nullptr;
  }
  DCHECK(socket->IsBlocking());
  if (socket->Connect(peer)) {
    return nullptr;
  }
  const absl::StatusOr<Spi> spi = psp::GetInitialRxSpi(fd);
  if (!spi.ok() || spi.value() != self_token->spi) {
    LOG(WARNING) << "failed to verify negotiated rx psp spi";
    return nullptr;
  }
  DCHECK(socket->IsConnected());
  LOG(INFO) << "made psp " << *socket;
  return socket;
}

const PspTcpManager::Listener* PspTcpManager::findListener(
    const Endpoint& target) const {
  DCHECK(target.HasNonzeroIpPort());
  for (const auto& [fd, listener] : listeners_) {
    if (target == listener.endpoint) return &listener;
  }
  return nullptr;
}

absl::StatusOr<PspToken> PspTcpManager::ExchangePspTokens(
    const PspToken& peer_token, const Endpoint& self_target) {
  DCHECK(peer_token.IsValid());
  DCHECK(self_target.HasNonzeroIpPort());

  const Listener* const l = findListener(self_target);
  if (l == nullptr) {
    return absl::NotFoundError(
        absl::StrCat("no listener found for ", self_target.ToString()));
  }
  const fd_t fd = l->socket->fd();
  absl::MutexLock lock(*l->mu);
  absl::StatusOr<PspToken> self_token = psp::AddSecureListener(fd, peer_token);
  if (!self_token.ok() || !self_token->IsValid()) {
    return absl::InternalError("invalid self psp token");
  }
  // RemoveSecureListener(fd) is automatically called when `fd` is closed.
  return self_token;
}

}  // namespace peregrine::internal
