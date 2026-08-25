#include "src/internal/socket/acceptor.h"

#include <sys/epoll.h>

#include <algorithm>
#include <atomic>
#include <cstdint>
#include <memory>
#include <utility>

#include "absl/base/optimization.h"
#include "absl/container/flat_hash_map.h"
#include "absl/log/check.h"
#include "absl/log/log.h"
#include "absl/memory/memory.h"
#include "src/internal/assumptions.h"
#include "src/internal/base/endpoint.h"
#include "src/internal/base/hostinfo.h"
#include "src/internal/base/types.h"
#include "src/internal/event/poller.h"
#include "src/internal/socket/socket_tcp.h"
#include "src/internal/socket/socket_util.h"

namespace peregrine::internal {

std::unique_ptr<TcpSocket> TcpAcceptor::createOne(Endpoint& endpoint,
                                                  Poller* poller) {
  DCHECK(!endpoint.HasZeroIpAddr());
  DCHECK_NE(poller, nullptr);

  static_assert(assumptions::kOnlyTcpListeningSocketsAreNonBlocking);
  const int family = endpoint.GetIpAddr().AddressFamily();
  constexpr bool kBlocking = false;
  std::unique_ptr<TcpSocket> socket = TcpSocket::Create(family, kBlocking);
  if ABSL_PREDICT_FALSE (socket == nullptr) {
    return nullptr;
  }

  DCHECK(socket->IsNonBlocking());
  if ABSL_PREDICT_FALSE (!socket->Listen(endpoint)) {
    return nullptr;
  }

  endpoint = Endpoint::Create(SelfAddrPort(socket->fd()));
  if (!endpoint.HasNonzeroIpPort()) {
    return nullptr;
  }

  constexpr uint32_t kEvents = EPOLLIN | EPOLLRDHUP | EPOLLET;
  if ABSL_PREDICT_FALSE (!poller->Register(socket->fd(), kEvents)) {
    return nullptr;
  }

  LOG(INFO) << "created, " << *socket;
  return socket;
}

namespace {
void FillListenerIpAddrs(HostInfo& self) {
  // TODO(mubashirq): scan nics and find all routable ipv{4,6} addrs.
  self.data_plane_listeners = {
      Endpoint(self.control_plane_listener.GetIpAddr(), 0),
  };
}
}  // namespace

std::unique_ptr<TcpAcceptor> TcpAcceptor::Create(HostInfo& self) {
  std::unique_ptr<Poller> poller = Poller::Create();
  if ABSL_PREDICT_FALSE (poller == nullptr) {
    return nullptr;
  }

  FillListenerIpAddrs(self);
  absl::flat_hash_map<fd_t, Listener> listeners;
  for (Endpoint& e : self.data_plane_listeners) {
    std::unique_ptr<TcpSocket> socket = createOne(e, poller.get());
    if ABSL_PREDICT_FALSE (socket == nullptr) {
      return nullptr;
    }
    DCHECK(e.HasNonzeroIpPort());
    const fd_t fd = socket->fd();
    listeners.emplace(fd, Listener(std::move(socket)));
  }
  DCHECK(self.IsValid());

  return absl::WrapUnique(
      new TcpAcceptor(self, std::move(poller), std::move(listeners)));
}

void TcpAcceptor::Start(AcceptCallback accept) {
  static_assert(assumptions::kOnlyTcpListeningSocketsAreNonBlocking);
  DCHECK(invariant());
  DCHECK_NE(accept, nullptr);
  LOG(INFO) << "starting, " << self_;

  constexpr int kMaxEvents = 64;
  epoll_event events[kMaxEvents];
  while (!stop_.load(std::memory_order_relaxed) && !listeners_.empty()) {
    const int nfds = poller_->BlockingWait(events, kMaxEvents);
    if (nfds < 0) {
      if (stop_.load(std::memory_order_relaxed)) break;
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
        const fd_t new_fd = listener->Accept();
        const int ret = new_fd.value();
        if ABSL_PREDICT_FALSE (ret < 0) {
          if (IsWouldBlock(ret) || IsShutdown(ret)) {
            // do nothing
          } else {
            LOG(WARNING) << "accept failed, " << *listener;
          }
          break;
        }
        std::unique_ptr<TcpSocket> socket = TcpSocket::Create(new_fd, family);
        DCHECK(socket->IsBlocking());
        DCHECK(socket->IsConnected());
        LOG(INFO) << "made " << *socket;
        accept(std::move(socket));
      }
    }
  }
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
