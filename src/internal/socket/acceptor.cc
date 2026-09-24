#include "src/internal/socket/acceptor.h"

#include <sys/epoll.h>

#include <algorithm>
#include <atomic>
#include <cstdint>
#include <memory>
#include <string>
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
#include "src/internal/assumptions.h"
#include "src/internal/base/endpoint.h"
#include "src/internal/base/hostinfo.h"
#include "src/internal/base/nicinfo.h"
#include "src/internal/base/types.h"
#include "src/internal/event/poller.h"
#include "src/internal/socket/psp/psp.h"
#include "src/internal/socket/psp/psp_util.h"
#include "src/internal/socket/socket_tcp.h"
#include "src/internal/socket/socket_util.h"
#include "src/util/nic.h"

namespace peregrine::internal {

std::unique_ptr<TcpSocket> TcpAcceptor::createOne(Endpoint& endpoint,
                                                  Poller& poller) {
  DCHECK(!endpoint.HasZeroIpAddr());

  static_assert(assumptions::kOnlyTcpListeningSocketsAreNonBlocking);
  const int family = endpoint.GetIpAddr().AddressFamily();
  constexpr bool kBlocking = false;
  std::unique_ptr<TcpSocket> socket = TcpSocket::Create(family, kBlocking);
  if ABSL_PREDICT_FALSE (socket == nullptr) {
    return nullptr;
  }

  DCHECK(socket->IsNonBlocking());
  if ABSL_PREDICT_FALSE (socket->Listen(endpoint)) {
    return nullptr;
  }

  endpoint = SelfEndpoint(socket->fd());
  if (!endpoint.HasNonzeroIpPort()) {
    return nullptr;
  }

  constexpr uint32_t kEvents = EPOLLIN | EPOLLRDHUP | EPOLLET;
  if ABSL_PREDICT_FALSE (!poller.Register(socket->fd(), kEvents)) {
    return nullptr;
  }

  LOG(INFO) << "created, " << *socket;
  return socket;
}

namespace {
auto GetTcpListenerCandidates(const bool loopback) {
  absl::flat_hash_map<std::string, NicInfo> candidates;
  for (const auto& [ifc, nis] : util::FindRoutableIpAddrs()) {
    if (nis.type != util::NicType::kIP) continue;
    std::vector<Endpoint> es;
    for (const auto& ip : nis.addrs) {
      if (!loopback && ip.IsLoopback()) continue;
      es.emplace_back(ip, /*port=*/0);
    }
    if (es.empty()) continue;
    candidates.emplace(ifc, NicInfo(ifc, nis.type, std::move(es)));
  }
  return candidates;
}
}  // namespace

std::unique_ptr<TcpAcceptor> TcpAcceptor::Create(HostInfo& self) {
  std::unique_ptr<Poller> poller = Poller::Create();
  if ABSL_PREDICT_FALSE (poller == nullptr) {
    LOG(ERROR) << "failed to create poller";
    return nullptr;
  }

  const bool loopback = self.control_plane_listener.GetIpAddr().IsLoopback();
  auto candidates = GetTcpListenerCandidates(loopback);
  if ABSL_PREDICT_FALSE (candidates.empty()) {
    LOG(ERROR) << "failed to find data plane tcp listener candidates";
    return nullptr;
  }

  absl::flat_hash_map<fd_t, Listener> listeners;
  for (auto& [_, ni] : candidates) {
    NicInfo nic(ni.name, ni.type, {});
    for (auto& e : ni.endpoints) {  // `e` will be modified in createOne().
      std::unique_ptr<TcpSocket> socket = createOne(e, *poller);
      if ABSL_PREDICT_FALSE (socket == nullptr) {
        LOG(WARNING) << "failed to create tcp listening socket for " << e;
        continue;
      }
      const fd_t fd = socket->fd();
      DCHECK(e.HasNonzeroIpPort());
      DCHECK_EQ(e, SelfEndpoint(fd));
      listeners.emplace(fd, Listener{std::move(socket), e});
      nic.endpoints.push_back(e);
    }
    if (nic.IsValid()) {
      self.data_plane_listeners.push_back(nic);
    }
  }
  if ABSL_PREDICT_FALSE (!self.IsValid()) {
    LOG(ERROR) << "invalid self host info " << self;
    return nullptr;
  }

  return absl::WrapUnique(
      new TcpAcceptor(self, std::move(poller), std::move(listeners)));
}

void TcpAcceptor::Start(OnAccept on_accept, bool gen_blocking) {
  static_assert(assumptions::kOnlyTcpListeningSocketsAreNonBlocking);
  DCHECK(invariant());
  DCHECK_NE(on_accept, nullptr);
  LOG(INFO) << "starting, " << self_;

  constexpr int kMaxEvents = 64;
  epoll_event events[kMaxEvents];
  while (!isStopped() && !listeners_.empty()) {
    const int nfds = poller_->BlockingWait(events, kMaxEvents);
    if (nfds < 0) {
      if (isStopped()) break;
      LOG(WARNING) << "poller wait failed";
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
        const fd_t new_fd = listener->Accept(gen_blocking);
        const int ret = new_fd.value();
        if ABSL_PREDICT_FALSE (ret < 0) {
          if ABSL_PREDICT_FALSE (IsOutOfResource(ret)) {
            LOG_EVERY_N_SEC(ERROR, 3) << "out of resource, " << *listener;
            absl::SleepFor(absl::Milliseconds(100));  // avoid busy-looping
          } else if (IsWouldBlock(ret) || IsShutdown(ret)) {
            // do nothing
          } else {
            LOG(WARNING) << "accept failed, " << *listener;
          }
          break;
        }
        std::unique_ptr<TcpSocket> socket = TcpSocket::Create(new_fd, family);
        DCHECK_EQ(socket->IsBlocking(), gen_blocking);
        DCHECK(socket->IsConnected());
        LOG(INFO) << "made " << *socket;
        on_accept(std::move(socket));
      }
    }
  }
}

const TcpAcceptor::Listener* TcpAcceptor::findListener(
    const Endpoint& target) const {
  DCHECK(target.HasNonzeroIpPort());
  for (const auto& [fd, listener] : listeners_) {
    if (target == listener.endpoint) return &listener;
  }
  return nullptr;
}

absl::StatusOr<PspToken> TcpAcceptor::ExchangePspTokens(
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
  return self_token;  // TODO(yyd): RemoveSecureListener when `fd` is closed?
}

void TcpAcceptor::Stop() {
  DCHECK(invariant());
  stop_.store(true, std::memory_order_relaxed);
  for (const auto& [fd, listener] : listeners_) {
    listener.socket->Shutdown();  // unblocks BlockingWait() above
  }
  LOG(INFO) << "stopped, " << self_;
}

bool TcpAcceptor::invariant() const {
  const bool nonblocking =
      std::all_of(listeners_.begin(), listeners_.end(), [](const auto& pair) {
        return pair.first == pair.second.socket->fd() &&
               pair.second.socket->IsNonBlocking();
      });
  return self_.IsValid() && poller_ != nullptr && nonblocking;
}

}  // namespace peregrine::internal
