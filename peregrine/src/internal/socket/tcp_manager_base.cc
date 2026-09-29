#include "peregrine/src/internal/socket/tcp_manager_base.h"

#include <algorithm>
#include <memory>
#include <utility>
#include <vector>

#include "absl/base/optimization.h"
#include "absl/container/flat_hash_map.h"
#include "absl/container/node_hash_map.h"
#include "absl/functional/function_ref.h"
#include "absl/log/check.h"
#include "absl/log/log.h"
#include "absl/synchronization/mutex.h"
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
  return std::all_of(fd2skts_.begin(), fd2skts_.end(), [](const auto& pair) {
    return IsPairAndNonBlocking(pair.first, pair.second.socket.get());
  });
}

const TcpSocket* TcpManagerBase::Listeners::Get(const fd_t fd) const {
  absl::MutexLock _(mu_);
  const auto it = fd2skts_.find(fd);
  return it != fd2skts_.end() ? it->second.socket.get() : nullptr;
}

void TcpManagerBase::Listeners::Remove(const fd_t fd) {
  absl::MutexLock _(mu_);
  fd2skts_.erase(fd);
}

void TcpManagerBase::Listeners::Close() {
  absl::node_hash_map<fd_t, Listener> doomed;
  {
    absl::MutexLock _(mu_);
    doomed.swap(fd2skts_);
  }
  doomed.clear();  // sockets are closed outside the lock
}

void TcpManagerBase::Listeners::Shutdown() {
  absl::MutexLock _(mu_);
  for (auto& [_, listener] : fd2skts_) {
    listener.socket->Shutdown();
  }
}

bool TcpManagerBase::Connectors::Invariant() const {
  absl::MutexLock _(mu_);
  return std::all_of(fd2skts_.begin(), fd2skts_.end(), [](const auto& pair) {
    return IsPair(pair.first, pair.second.socket.get());
  });
}

bool TcpManagerBase::Connectors::Add(const fd_t fd,
                                     std::unique_ptr<TcpSocket> socket,
                                     absl::FunctionRef<bool(fd_t)> on_add) {
  DCHECK_EQ(fd, socket->fd());
  DCHECK(!socket->IsConnected());
  {
    absl::MutexLock _(mu_);
    if ABSL_PREDICT_TRUE (!closed_ && on_add(fd)) {
      LOG(INFO) << "added connector " << *socket;
      fd2skts_.emplace(fd, Connector{std::move(socket)});
      return true;
    }
  }
  LOG(WARNING) << "rejected connector " << *socket;
  return false;  // `socket` is closed outside the lock
}

std::unique_ptr<TcpSocket> TcpManagerBase::Connectors::Remove(const fd_t fd) {
  absl::MutexLock _(mu_);
  const auto it = fd2skts_.find(fd);
  if ABSL_PREDICT_FALSE (it == fd2skts_.end()) {
    return nullptr;
  } else {
    std::unique_ptr<TcpSocket> socket = std::move(it->second.socket);
    fd2skts_.erase(it);
    DCHECK(!socket->IsConnected());
    return socket;
  }
}

void TcpManagerBase::Connectors::Close() {
  absl::flat_hash_map<fd_t, Connector> doomed;
  {
    absl::MutexLock _(mu_);
    closed_ = true;
    doomed.swap(fd2skts_);
  }
  doomed.clear();  // sockets are closed outside the lock
}

bool TcpManagerBase::Produced::Invariant() const {
  absl::MutexLock _(mu_);
  return std::all_of(sockets_.begin(), sockets_.end(), [](const auto& s) {
    return s != nullptr && s->IsConnected();
  });
}

bool TcpManagerBase::Produced::Add(std::unique_ptr<TcpSocket> socket) {
  DCHECK_NE(socket, nullptr);
  DCHECK(socket->IsConnected());
  {
    absl::MutexLock _(mu_);
    if ABSL_PREDICT_TRUE (!closed_) {
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
    closed_ = true;
    doomed.swap(sockets_);
  }
  doomed.clear();  // sockets are closed outside the lock
}

}  // namespace peregrine::internal
