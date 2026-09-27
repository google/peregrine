#include "peregrine/src/internal/socket/tcp_manager.h"

#include <sys/epoll.h>

#include <atomic>
#include <cstdint>
#include <memory>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "absl/base/optimization.h"
#include "absl/container/flat_hash_map.h"
#include "absl/container/node_hash_map.h"
#include "absl/log/check.h"
#include "absl/log/log.h"
#include "absl/memory/memory.h"
#include "absl/strings/str_format.h"
#include "absl/time/clock.h"
#include "absl/time/time.h"
#include "peregrine/src/internal/assumptions.h"
#include "peregrine/src/internal/base/endpoint.h"
#include "peregrine/src/internal/base/hostinfo.h"
#include "peregrine/src/internal/base/nicinfo.h"
#include "peregrine/src/internal/base/types.h"
#include "peregrine/src/internal/event/poller.h"
#include "peregrine/src/internal/socket/socket_tcp.h"
#include "peregrine/src/internal/socket/socket_tcp_util.h"
#include "peregrine/src/internal/socket/socket_util.h"
#include "peregrine/src/util/nic.h"

namespace peregrine::internal {

namespace {
constexpr uint32_t kError = EPOLLERR | EPOLLHUP;
constexpr uint32_t kIncomingEvents = EPOLLIN | kError;

std::string EvtMsg(std::string_view what, const fd_t fd, const uint32_t flag) {
  return absl::StrFormat("%s fd=%d events=%d @ %s ", what, fd.value(), flag,
                         AddrPortPair(fd));
}
}  // namespace

std::unique_ptr<TcpSocket> TcpManager::createListener(Endpoint& endpoint,
                                                      Poller& poller) {
  DCHECK(!endpoint.HasZeroIpAddr());

  static_assert(assumptions::kOnlyTcpListeningSocketsAreNonBlocking);
  const int family = endpoint.GetIpAddr().AddressFamily();
  auto socket = TcpSocket::Create(family, /*blocking=*/false);
  if ABSL_PREDICT_FALSE (socket == nullptr) {
    return nullptr;
  }
  DCHECK(socket->IsNonBlocking());
  if ABSL_PREDICT_FALSE (socket->Listen(endpoint)) {
    return nullptr;
  }
  const Endpoint e = SelfEndpoint(socket->fd());
  if ABSL_PREDICT_FALSE (!e.HasNonzeroIpPort()) {
    return nullptr;
  }
  DCHECK_EQ(kIncomingEvents & EPOLLET, 0);  // level-triggered
  if ABSL_PREDICT_FALSE (!poller.Register(socket->fd(), kIncomingEvents)) {
    return nullptr;
  }
  endpoint = e;
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

std::unique_ptr<TcpManager> TcpManager::Create(HostInfo& self) {
  std::unique_ptr<Poller> poller = Poller::Create();
  if ABSL_PREDICT_FALSE (poller == nullptr) {
    LOG(ERROR) << "failed to create event poller";
    return nullptr;
  }
  const bool loopback = self.control_plane_listener.GetIpAddr().IsLoopback();
  auto candidates = GetTcpListenerCandidates(loopback);
  if ABSL_PREDICT_FALSE (candidates.empty()) {
    LOG(ERROR) << "failed to find data plane tcp listener candidates";
    return nullptr;
  }
  // Pointer stability is required.
  absl::node_hash_map<fd_t, Listener> listeners;
  for (auto& [_, ni] : candidates) {
    NicInfo nic(ni.name, ni.type, {});
    for (auto& e : ni.endpoints) {  // `e` will be modified below
      if (auto socket = createListener(e, *poller); socket != nullptr) {
        const fd_t fd = socket->fd();
        DCHECK(e.HasNonzeroIpPort());
        DCHECK_EQ(e, SelfEndpoint(fd));
        listeners.emplace(fd, Listener{std::move(socket)});
        nic.endpoints.push_back(e);
      }
    }
    if (nic.IsValid()) self.data_plane_listeners.push_back(nic);
  }
  if ABSL_PREDICT_FALSE (!self.IsValid()) {
    LOG(ERROR) << "invalid self host info " << self;
    return nullptr;
  }
  auto m = new TcpManager(self, std::move(poller), std::move(listeners));
  return absl::WrapUnique(m);
}

TcpManager::TcpManager(const HostInfo& self, std::unique_ptr<Poller> poller,
                       absl::node_hash_map<fd_t, Listener> sockets)
    : self_(self),
      stop_(false),
      poller_(std::move(poller)),
      listeners_(std::move(sockets)) {
  DCHECK(invariant());
}

int TcpManager::Connect(const Endpoint& self, const Endpoint& peer,
                        const bool blocking) {
  DCHECK(peer.HasNonzeroIpPort());

  // TODO(yongx): non-blocking connect
  if (!blocking) return -1;

  const int family = peer.GetIpAddr().AddressFamily();
  auto socket = TcpSocket::Create(family, /*blocking=*/true);
  if ABSL_PREDICT_FALSE (socket == nullptr) {
    return -1;
  }
  if (!self.HasZeroIpAddr() && socket->Bind(self)) {
    return -1;
  }
  DCHECK(socket->IsBlocking());
  if (socket->Connect(peer)) {
    return -1;
  }
  DCHECK(socket->IsConnected());
  LOG(INFO) << "made " << *socket;
  connected_.Add(std::move(socket));
  return 0;
}

bool TcpManager::handleAllIncoming(OnAccept& on_accept, const bool gen_blocking,
                                   const fd_t fd, const uint32_t flag) {
  const TcpSocket* listener = listeners_.Get(fd);
  if (listener == nullptr) return false;

  if ABSL_PREDICT_FALSE (flag & (EPOLLERR | EPOLLHUP)) {
    // A listening socket only reports HUP/ERR after leaving TCP_LISTEN,
    // triggered by the Shutdown() call in Stop() below.
    LOG(WARNING) << EvtMsg("listening", fd, flag);
    poller_->Unregister(fd);
    listeners_.Remove(fd);

  } else if (flag & EPOLLIN) {
    // Listening sockets are level-triggered, not edge-triggered.
    constexpr int kMaxAcceptsPerEvent = 64;
    const int family = listener->family();
    for (int i = 0; i < kMaxAcceptsPerEvent && !isStopped(); ++i) {
      const int ret = listener->Accept(gen_blocking);
      if ABSL_PREDICT_FALSE (ret < 0) {
        if ABSL_PREDICT_FALSE (IsOutOfResource(ret)) {
          LOG_EVERY_N_SEC(ERROR, 3) << "out of resource, " << *listener;
          absl::SleepFor(absl::Milliseconds(100));  // avoid busy-looping
        } else if (!IsWouldBlock(ret) && !IsShutdown(ret)) {
          LOG_EVERY_N_SEC(ERROR, 1) << "accept failed, " << *listener;
        }
        break;
      }
      const fd_t new_fd(ret);
      std::unique_ptr<TcpSocket> socket = TcpSocket::Create(new_fd, family);
      DCHECK(socket->MatchesBlocking(gen_blocking));
      DCHECK(socket->IsConnected());
      LOG(INFO) << "made " << *socket;
      on_accept(std::move(socket));
    }
  }
  return true;
}

void TcpManager::Start(OnAccept on_accept, bool gen_blocking) {
  static_assert(assumptions::kOnlyTcpListeningSocketsAreNonBlocking);
  DCHECK(invariant());
  DCHECK_NE(on_accept, nullptr);
  LOG(INFO) << "starting, " << self_;

  constexpr int kTimeoutMs = 100;
  constexpr int kMaxEvents = 64;
  epoll_event events[kMaxEvents];
  while (!isStopped()) {
    const int nfds = poller_->BlockingWait(events, kMaxEvents, kTimeoutMs);
    if ABSL_PREDICT_FALSE (nfds < 0) {
      if (isStopped()) break;
      LOG(ERROR) << "poller wait failed";
      continue;
    }
    for (int i = 0; i < nfds; ++i) {
      const auto& e = events[i];
      const fd_t fd(e.data.fd);
      const uint32_t flag = e.events;
      if (!handleAllIncoming(on_accept, gen_blocking, fd, flag)) {
        LOG_EVERY_N_SEC(ERROR, 1) << EvtMsg("unhandled", fd, flag);
      }
    }
  }
  listeners_.Clear();
}

void TcpManager::Stop() {
  DCHECK(invariant());
  stop_.store(true, std::memory_order_relaxed);
  listeners_.Shutdown();  // unblocks BlockingWait() above
  connected_.Clear();
  LOG(INFO) << "stopped, " << self_;
}

bool TcpManager::invariant() const {
  return self_.IsValid() && poller_ != nullptr && listeners_.Invariant() &&
         connected_.Invariant();
}

}  // namespace peregrine::internal
