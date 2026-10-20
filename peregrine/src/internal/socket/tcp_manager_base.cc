#include "peregrine/src/internal/socket/tcp_manager_base.h"

#include <algorithm>
#include <memory>
#include <utility>
#include <vector>

#include "absl/base/optimization.h"
#include "absl/container/flat_hash_map.h"
#include "absl/functional/function_ref.h"
#include "absl/log/check.h"
#include "absl/log/log.h"
#include "absl/synchronization/mutex.h"
#include "absl/time/time.h"
#include "peregrine/src/internal/base/endpoint.h"
#include "peregrine/src/internal/base/types.h"
#include "peregrine/src/internal/socket/socket_tcp.h"

namespace peregrine::internal {

namespace {
inline bool IsPair(const fd_t fd, const TcpSocket* s) {
  return s != nullptr && fd == s->fd();
}
inline bool IsPairAndNonBlocking(const fd_t fd, const TcpSocket* s) {
  return IsPair(fd, s) && s->IsNonBlocking();
}
}  // namespace

bool TcpManagerBase::Listeners::Invariant() const {
  absl::MutexLock _(mu_);
  return std::all_of(fd2ls_.begin(), fd2ls_.end(), [](const auto& pair) {
    return IsPairAndNonBlocking(pair.first, pair.second.socket.get()) &&
           pair.second.endpoint == SelfEndpoint(pair.first) &&
           pair.second.endpoint.HasNonzeroIpPort();
  });
}

const TcpSocket* TcpManagerBase::Listeners::Get(const fd_t fd) const {
  absl::MutexLock _(mu_);
  const auto it = fd2ls_.find(fd);
  return it != fd2ls_.end() ? it->second.socket.get() : nullptr;
}

const TcpManagerBase::Listener* TcpManagerBase::Listeners::Find(
    const Endpoint& endpoint) const {
  absl::MutexLock _(mu_);
  for (const auto& [_, l] : fd2ls_) {
    if (l.endpoint == endpoint) return &l;
  }
  return nullptr;
}

void TcpManagerBase::Listeners::Remove(const fd_t fd) {
  absl::MutexLock _(mu_);
  fd2ls_.erase(fd);
}

void TcpManagerBase::Listeners::Shutdown() {
  absl::MutexLock _(mu_);
  for (auto& [_, l] : fd2ls_) {
    l.socket->Shutdown();
  }
}

void TcpManagerBase::Listeners::Close() {
  absl::flat_hash_map<fd_t, Listener> doomed;
  {
    absl::MutexLock _(mu_);
    doomed.swap(fd2ls_);
  }
  doomed.clear();  // sockets are closed outside the lock
}

bool TcpManagerBase::Connectors::Invariant() const {
  absl::MutexLock _(mu_);
  return std::all_of(fd2cs_.begin(), fd2cs_.end(), [](const auto& pair) {
    return IsPairAndNonBlocking(pair.first, pair.second.socket.get());
  });
}

void TcpManagerBase::Connectors::Seal() {
  absl::MutexLock _(mu_);
  sealed_ = true;
}

bool TcpManagerBase::Connectors::Add(const fd_t fd, Connector connector,
                                     absl::FunctionRef<bool(fd_t)> on_add) {
  DCHECK_EQ(fd, connector.socket->fd());
  DCHECK(!connector.socket->IsConnected());
  {
    absl::MutexLock _(mu_);
    if ABSL_PREDICT_TRUE (!sealed_ && !fd2cs_.contains(fd) && on_add(fd)) {
      LOG(INFO) << "added connector " << *connector.socket;
      fd2cs_.emplace(fd, std::move(connector));
      return true;
    }
  }
  LOG(WARNING) << "rejected connector " << *connector.socket;
  return false;  // `socket` is closed outside the lock
}

std::unique_ptr<TcpSocket> TcpManagerBase::Connectors::Remove(const fd_t fd) {
  absl::MutexLock _(mu_);
  const auto it = fd2cs_.find(fd);
  if ABSL_PREDICT_FALSE (it == fd2cs_.end()) {
    return nullptr;
  } else {
    std::unique_ptr<TcpSocket> socket = std::move(it->second.socket);
    fd2cs_.erase(it);
    DCHECK(!socket->IsConnected());
    return socket;
  }
}

std::vector<TcpManagerBase::Connector>
TcpManagerBase::Connectors::RemoveExpired(const absl::Duration now) {
  std::vector<Connector> expired;
  {
    absl::MutexLock _(mu_);
    for (auto it = fd2cs_.begin(); it != fd2cs_.end();) {
      const auto cur = it++;
      if (Connector& c = cur->second; c.expiration <= now) {
        DCHECK(!c.socket->IsConnected());
        expired.emplace_back(std::move(c));
        fd2cs_.erase(cur);
      }
    }
  }
  return expired;
}

void TcpManagerBase::Connectors::Close() {
  absl::flat_hash_map<fd_t, Connector> doomed;
  {
    absl::MutexLock _(mu_);
    DCHECK(sealed_);
    doomed.swap(fd2cs_);
  }
  doomed.clear();  // sockets are closed outside the lock
}

bool TcpManagerBase::Produced::Invariant() const {
  absl::MutexLock _(mu_);
  return std::all_of(sockets_.begin(), sockets_.end(), [](const auto& s) {
    return s != nullptr && s->IsConnected();
  });
}

void TcpManagerBase::Produced::Seal() {
  absl::MutexLock _(mu_);
  sealed_ = true;
}

bool TcpManagerBase::Produced::Add(std::unique_ptr<TcpSocket> socket) {
  DCHECK_NE(socket, nullptr);
  DCHECK(socket->IsConnected());
  {
    absl::MutexLock _(mu_);
    if ABSL_PREDICT_TRUE (!sealed_) {
      LOG(INFO) << "added socket " << *socket;
      sockets_.emplace_back(std::move(socket));
      return true;
    }
  }
  LOG(WARNING) << "rejected socket " << *socket;
  return false;
}

std::vector<std::unique_ptr<TcpSocket>> TcpManagerBase::Produced::MoveAll() {
  absl::MutexLock _(mu_);
  return std::exchange(sockets_, {});
}

void TcpManagerBase::Produced::Close() {
  std::vector<std::unique_ptr<TcpSocket>> doomed;
  {
    absl::MutexLock _(mu_);
    DCHECK(sealed_);
    doomed.swap(sockets_);
  }
  doomed.clear();  // sockets are closed outside the lock
}

}  // namespace peregrine::internal
