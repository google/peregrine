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
#include "absl/container/node_hash_map.h"
#include "absl/log/check.h"
#include "absl/log/log.h"
#include "absl/memory/memory.h"
#include "absl/strings/str_format.h"
#include "absl/time/clock.h"
#include "absl/time/time.h"
#include "peregrine/src/internal/assumptions.h"
#include "peregrine/src/internal/base/constants.h"
#include "peregrine/src/internal/base/endpoint.h"
#include "peregrine/src/internal/base/hostinfo.h"
#include "peregrine/src/internal/base/nicinfo.h"
#include "peregrine/src/internal/base/types.h"
#include "peregrine/src/internal/event/poller.h"
#include "peregrine/src/internal/socket/socket_error.h"
#include "peregrine/src/internal/socket/socket_tcp.h"
#include "peregrine/src/internal/socket/socket_util.h"

namespace peregrine::internal {

namespace {
// ERR/HUP are always reported, but RDHUP must be requested.
// Level-trigger is used for both listening and connecting sockets.
constexpr uint32_t kIncomingEvents = EPOLLIN;
constexpr uint32_t kOutgoingEvents = EPOLLOUT | EPOLLRDHUP;

std::string EvtMsg(std::string_view what, const fd_t fd, const uint32_t flag,
                   const int err = 0) {
  return absl::StrFormat("%s fd=%d events=%#x errno=%d @ %s", what, fd.value(),
                         flag, err, AddrPortPair(fd));
}
}  // namespace

std::unique_ptr<TcpSocket> TcpManager::createListener(Endpoint& endpoint,
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
  if ABSL_PREDICT_FALSE (!e.HasNonzeroIpPort()) {
    return nullptr;
  }
  static_assert((kIncomingEvents & EPOLLET) == 0);
  if ABSL_PREDICT_FALSE (poller.Register(socket->fd(), kIncomingEvents) < 0) {
    return nullptr;
  }
  endpoint = e;
  LOG(INFO) << "created, " << *socket;
  return socket;
}

std::unique_ptr<TcpManager> TcpManager::Create(HostInfo& self) {
  std::unique_ptr<Poller> poller = Poller::Create();
  if ABSL_PREDICT_FALSE (poller == nullptr) {
    LOG(ERROR) << "failed to create event poller";
    return nullptr;
  }
  const bool loopback = self.control_plane_listener.GetIpAddr().IsLoopback();
  auto candidates = NicInfo::GetTcpListenerCandidates(loopback);
  if ABSL_PREDICT_FALSE (candidates.empty()) {
    LOG(ERROR) << "failed to find data plane tcp listener candidates";
    return nullptr;
  }
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
  if ABSL_PREDICT_FALSE (listeners.empty()) {
    LOG(ERROR) << "failed to create data plane tcp listening sockets";
    return nullptr;
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
  static_assert(assumptions::kTcpConnectingSocketsCanBeBlockingOrNonBlocking);
  DCHECK(peer.HasNonzeroIpPort());

  if ABSL_PREDICT_FALSE (isStopped()) return kConnectError;

  const int family = peer.GetIpAddr().AddressFamily();
  auto socket = TcpSocket::Create(family, blocking);
  if ABSL_PREDICT_FALSE (socket == nullptr) {
    return kConnectError;
  } else if (!self.HasZeroIpAddr() && socket->Bind(self) < 0) {
    return kConnectError;
  } else {
    return blocking ? connectBlocking(std::move(socket), peer)
                    : connectNonBlocking(std::move(socket), peer);
  }
}

int TcpManager::addConnected(std::unique_ptr<TcpSocket> socket) {
  DCHECK(socket->IsConnected());
  LOG(INFO) << "made " << *socket;
  return outgoing_.Add(std::move(socket)) ? kConnectSuccess : kConnectError;
}

int TcpManager::connectBlocking(std::unique_ptr<TcpSocket> socket,
                                const Endpoint& peer) {
  DCHECK(socket->IsBlocking());
  if (socket->Connect(peer) < 0) return kConnectError;
  return addConnected(std::move(socket));
}

int TcpManager::connectNonBlocking(std::unique_ptr<TcpSocket> socket,
                                   const Endpoint& peer) {
  DCHECK(socket->IsNonBlocking());
  switch (const int ret = socket->Connect(peer); ret) {
    case kConnectSuccess:
      return addConnected(std::move(socket));
    case kConnectInProgress: {
      DCHECK(!socket->IsConnected());
      LOG(INFO) << "connecting " << *socket;
      // Registers under the connectors_ lock, so that Close() cannot close
      // (and the OS reuse) `fd` before registration, and the poller thread
      // cannot see an event for `fd` before it is added.
      const fd_t fd = socket->fd();
      const auto register_fd = [this](const fd_t added) {
        static_assert((kOutgoingEvents & EPOLLET) == 0);
        return poller_->Register(added, kOutgoingEvents) == 0;
      };
      return connectors_.Add(fd, std::move(socket), register_fd)
                 ? kConnectInProgress
                 : kConnectError;
    }
    default:
      return kConnectError;
  }
}

void TcpManager::removeListener(const fd_t fd) {
  poller_->Unregister(fd);
  listeners_.Remove(fd);
}

bool TcpManager::handleAllIncoming(const fd_t fd, const uint32_t flag,
                                   const bool gen_blocking) {
  const TcpSocket* listener = listeners_.Get(fd);
  if (listener == nullptr) return false;

  if ABSL_PREDICT_FALSE (flag & (EPOLLERR | EPOLLHUP)) {
    // A listening socket only reports HUP/ERR after leaving TCP_LISTEN,
    // triggered by the Shutdown() call in Stop() below.
    LOG(WARNING) << EvtMsg("listening", fd, flag);
    removeListener(fd);

  } else if (flag & EPOLLIN) {
    // Listening sockets are level-triggered, not edge-triggered.
    const int family = listener->family();
    for (int i = 0; i < kEpollMaxAcceptsPerEvent && !isStopped(); ++i) {
      const int ret = listener->Accept(gen_blocking);
      if ABSL_PREDICT_FALSE (ret < 0) {
        if (IsWouldBlock(ret)) {
          // All incoming connections processed.
        } else if (IsShutdown(ret)) {
          LOG(INFO) << EvtMsg("listening", fd, flag);
          removeListener(fd);
        } else if (IsOutOfResource(ret)) {
          LOG_EVERY_N_SEC(ERROR, 3) << "accept out of resource, " << *listener;
          absl::SleepFor(absl::Milliseconds(50));  // avoid busy-looping
        } else {
          LOG_EVERY_N_SEC(ERROR, 1) << "accept failed, " << *listener;
          absl::SleepFor(absl::Milliseconds(10));  // avoid busy-looping
        }
        break;
      }
      const fd_t new_fd(ret);
      auto socket = TcpSocket::Create(new_fd, family);
      DCHECK(socket->MatchesBlocking(gen_blocking));
      DCHECK(socket->IsConnected());
      LOG(INFO) << "made " << *socket;
      if (!incoming_.Add(std::move(socket))) break;
    }
  }
  return true;
}

bool TcpManager::handleOneOutgoing(const fd_t fd, const uint32_t flag) {
  std::unique_ptr<TcpSocket> socket = connectors_.Remove(fd);
  if (socket == nullptr) return false;

  // Must precede closing `fd` at scope exit.
  poller_->Unregister(fd);

  const int err = GetSocketError(fd);
  if ABSL_PREDICT_FALSE (err != 0 || (flag & (EPOLLERR | EPOLLHUP)) ||
                         !(flag & EPOLLOUT)) {
    LOG_EVERY_N_SEC(ERROR, 1) << EvtMsg("connecting", fd, flag, err);
  } else if ABSL_PREDICT_FALSE (flag & EPOLLRDHUP) {
    LOG_EVERY_N_SEC(WARNING, 1) << EvtMsg("peer hung up", fd, flag, err);
  } else {
    DCHECK(socket->IsNonBlocking());
    DCHECK(!socket->IsConnected());
    socket->SetConnected();
    DCHECK(socket->IsConnected());
    (void)addConnected(std::move(socket));
  }
  return true;
}

void TcpManager::Start(bool gen_blocking) {
  static_assert(assumptions::kTcpListeningSocketsAreNonBlocking);
  static_assert(assumptions::kTcpConnectingSocketsCanBeBlockingOrNonBlocking);
  DCHECK(invariant());
  LOG(INFO) << "starting, " << self_;

  epoll_event events[kEpollMaxNumEvents];
  while (!isStopped()) {
    const int nfds =
        poller_->BlockingWait(events, kEpollMaxNumEvents, kEpollWaitTimeoutMs);
    if (isStopped()) break;
    if (nfds > 0) {
      for (int i = 0; i < nfds; ++i) {
        const auto& e = events[i];
        const fd_t fd(e.data.fd);
        const uint32_t flag = e.events;
        if (handleAllIncoming(fd, flag, gen_blocking)) continue;
        if (handleOneOutgoing(fd, flag)) continue;
        LOG_EVERY_N_SEC(ERROR, 1) << EvtMsg("unhandled", fd, flag);
        poller_->Unregister(fd);  // stop it from firing again
      }
    } else if (nfds < 0) {
      LOG_EVERY_N_SEC(ERROR, 1) << "poller wait failed";
      absl::SleepFor(absl::Milliseconds(10));  // avoid busy-looping
    }
  }
  listeners_.Close();
  connectors_.Close();
}

void TcpManager::Stop() {
  DCHECK(invariant());
  stop_.store(true, std::memory_order_relaxed);
  listeners_.Shutdown();  // unblocks BlockingWait() above
  incoming_.Close();
  outgoing_.Close();
  LOG(INFO) << "stopped, " << self_;
}

bool TcpManager::invariant() const {
  return self_.IsValid() && poller_ != nullptr && listeners_.Invariant() &&
         connectors_.Invariant() && incoming_.Invariant() &&
         outgoing_.Invariant();
}

}  // namespace peregrine::internal
