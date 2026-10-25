#include "peregrine/src/api/v1/conn_pool.h"

#include <poll.h>
#include <sys/socket.h>
#include <unistd.h>

#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include "absl/base/optimization.h"
#include "absl/log/check.h"
#include "absl/log/log.h"
#include "absl/log/vlog_is_on.h"
#include "absl/status/status.h"
#include "absl/status/status_macros.h"
#include "absl/status/statusor.h"
#include "absl/strings/match.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/string_view.h"
#include "absl/synchronization/mutex.h"
#include "absl/time/time.h"
#include "grpcpp/channel.h"
#include "peregrine/src/api/v1/conn_util.h"
#include "peregrine/src/api/v1/ip_util.h"
#include "peregrine/src/util/clock.h"

namespace peregrine::v1 {

namespace {
bool HasReadableData(const int fd) {
  struct pollfd pfd = {.fd = fd, .events = POLLIN};
  return poll(&pfd, /*nfds=*/1, /*timeout=*/0) > 0;
}

void CloseSocket(const int fd) {
  DCHECK_GE(fd, 0);
  ::shutdown(fd, SHUT_RDWR);
  ::close(fd);
}
}  // namespace

absl::StatusOr<int> ConnPool::Borrow(absl::string_view peer,
                                     absl::string_view local_ip,
                                     bool require_psp,
                                     std::shared_ptr<grpc::Channel> channel) {
  const bool diag_vlog = VLOG_IS_ON(1);
  const absl::Duration borrow_start = ABSL_PREDICT_FALSE(diag_vlog)
                                          ? util::MonotonicNow()
                                          : absl::ZeroDuration();
  const Key key = GenPoolKey(peer, local_ip);
  int reused_fd = -1;
  size_t current_pool_size = 0;
  {
    absl::MutexLock lock(mu_);
    if (stop_) {
      return absl::FailedPreconditionError("ConnPool is closed.");
    }
    auto it = pool_.find(key);
    if ABSL_PREDICT_TRUE (it != pool_.end()) {
      Fds& fds = it->second;
      while (!fds.empty()) {
        const int fd = fds.back();
        fds.pop_back();

        if (HasReadableData(fd)) {
          CloseSocket(fd);
          continue;
        }
        reused_fd = fd;
        break;
      }
      current_pool_size = fds.size();
    }
  }
  if (reused_fd >= 0) {
    if (ABSL_PREDICT_FALSE(diag_vlog)) {
      const auto t = util::MonotonicNow() - borrow_start;
      if (const int64_t wait_ms = absl::ToInt64Milliseconds(t); wait_ms >= 10) {
        VLOG(1) << "conn borrow_wait wait_ms=" << wait_ms << " peer=" << peer
                << " local_ip=" << local_ip;
      }
    }
    return reused_fd;
  }

  ConnectTiming timing;
  ABSL_ASSIGN_OR_RETURN(
      int sock_fd,
      ConnectToPeer(peer, local_ip, diag_vlog ? &timing : nullptr));
  if (ABSL_PREDICT_FALSE(diag_vlog)) {
    std::string local_endpoint = GetLocalEndpoint(sock_fd);
    std::string psp_str =
        require_psp ? absl::StrCat(" psp_kex_us=", timing.psp_key_xchg_us) : "";
    VLOG(1) << "conn new_socket fd=" << sock_fd << " peer=" << peer
            << " local_ip=" << (local_ip.empty() ? local_endpoint : local_ip)
            << " bound_local=" << local_endpoint
            << " connect_us=" << timing.connect_us << psp_str
            << " pool_size=" << current_pool_size;
  }
  return sock_fd;
}

void ConnPool::Return(bool ok, int fd, absl::string_view peer,
                      absl::string_view local_ip) {
  if ABSL_PREDICT_FALSE (fd < 0) {
    return;
  }

  DCHECK_GE(fd, 0);
  if ABSL_PREDICT_FALSE (!ok) {
    CloseSocket(fd);
    return;
  }

  absl::MutexLock lock(mu_);
  if ABSL_PREDICT_FALSE (stop_) {
    CloseSocket(fd);
  } else {
    const Key key = GenPoolKey(peer, local_ip);
    pool_[key].push_back(fd);
  }
}

size_t ConnPool::CloseIdle(absl::string_view peer) {
  // Pool keys are "<local_ip>-><peer>", so the "->" delimiter makes a suffix
  // match exact for the peer regardless of the local IP the stream bound.
  const std::string suffix = GenPoolKeySuffix(peer);
  std::vector<int> to_close;
  {
    absl::MutexLock lock(mu_);
    for (auto it = pool_.begin(); it != pool_.end();) {
      if (!absl::EndsWith(it->first, suffix)) {
        ++it;
        continue;
      }
      to_close.insert(to_close.end(), it->second.begin(), it->second.end());
      pool_.erase(it++);
    }
  }
  for (const int fd : to_close) {
    CloseSocket(fd);
  }
  return to_close.size();
}

size_t ConnPool::IdleCount(absl::string_view peer) const {
  const std::string suffix = GenPoolKeySuffix(peer);
  size_t count = 0;
  absl::MutexLock lock(mu_);
  for (const auto& [key, fds] : pool_) {
    if (absl::EndsWith(key, suffix)) count += fds.size();
  }
  return count;
}

void ConnPool::Close() {
  absl::MutexLock lock(mu_);
  stop_ = true;
  for (auto& [_, fds] : pool_) {
    for (const int fd : fds) {
      CloseSocket(fd);
    }
  }
  pool_.clear();
}

}  // namespace peregrine::v1
