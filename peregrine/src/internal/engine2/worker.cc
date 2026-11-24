#include "peregrine/src/internal/engine2/worker.h"

#include <sys/epoll.h>
#include <sys/eventfd.h>
#include <unistd.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <cerrno>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <utility>
#include <vector>

#include "absl/base/optimization.h"
#include "absl/container/flat_hash_set.h"
#include "absl/log/check.h"
#include "absl/log/log.h"
#include "absl/synchronization/mutex.h"
#include "peregrine/src/internal/base/endpoint.h"
#include "peregrine/src/internal/base/hostinfo.h"
#include "peregrine/src/internal/base/types.h"
#include "peregrine/src/internal/channel/channel.h"
#include "peregrine/src/internal/engine2/assumptions.h"
#include "peregrine/src/internal/engine2/peer_work_queue.h"
#include "peregrine/src/internal/engine2/tcp_channel_session.h"
#include "peregrine/src/internal/event/poller.h"
#include "peregrine/src/internal/request/request_tracker.h"
#include "peregrine/src/util/thread.h"

namespace peregrine::internal::engine2 {
namespace {

constexpr int kMaxEvents = 64;
constexpr uint32_t kChannelEvents = EPOLLIN | EPOLLOUT | EPOLLRDHUP | EPOLLET;

}  // namespace

Worker::Worker(const int id, const int32_t rail_id, const HostInfo& self,
               RequestTracker& outgoing, RequestTracker& incoming)
    : id_(id),
      rail_id_(rail_id),
      wake_fd_(fd_t(::eventfd(/*initval=*/0, EFD_NONBLOCK | EFD_CLOEXEC))),
      self_(self),
      outgoing_(outgoing),
      incoming_(incoming),
      poller_(Poller::Create()) {
  CHECK(poller_ != nullptr);
  CHECK_GE(wake_fd_.value(), 0);
  CHECK_EQ(poller_->Register(wake_fd_, EPOLLIN), 0);
  thread_ = util::Jthread([this] { run(); });
}

Worker::~Worker() {
  {
    absl::MutexLock lock(inbox_mu_);
    stop_.store(true, std::memory_order_release);
    sleeping_ = false;
  }
  signalWake();
  if (thread_.joinable()) thread_.join();
  sessions_.clear();
  peer_fds_.clear();
  if (wake_fd_.value() >= 0) ::close(wake_fd_.value());
}

void Worker::AddChannel(const PeerId peer, std::unique_ptr<Channel> ch,
                        PeerWorkQueue& work_queue) {
  if (ABSL_PREDICT_FALSE(ch == nullptr)) return;
  bool should_signal = false;
  {
    absl::MutexLock lock(inbox_mu_);
    new_channels_.push_back(NewChannel{
        .peer = peer,
        .channel = std::move(ch),
        .work_queue = &work_queue,
    });
    has_inbox_work_.store(true, std::memory_order_release);
    should_signal = std::exchange(sleeping_, false);
  }
  if (should_signal) signalWake();
}

void Worker::Wake(const PeerId& peer) {
  bool should_signal = false;
  {
    absl::MutexLock lock(inbox_mu_);
    woken_peers_.insert(peer);
    has_inbox_work_.store(true, std::memory_order_release);
    should_signal = std::exchange(sleeping_, false);
  }
  if (should_signal) signalWake();
}

void Worker::signalWake() {
  const uint64_t one = 1;
  while (ABSL_PREDICT_FALSE(::write(wake_fd_.value(), &one, sizeof(one)) < 0)) {
    if (errno == EINTR) continue;
    if (errno != EAGAIN) {
      LOG_EVERY_N_SEC(WARNING, 1)
          << "worker #" << id_ << " wake_fd write failed: errno=" << errno;
    }
    break;
  }
}

void Worker::run() {
  std::array<epoll_event, kMaxEvents> events = {};

  while (!stop_.load(std::memory_order_acquire)) {
    // 1. Wait for events; sleep only if local queues and inbox are empty.
    int timeout_ms = 0;
    if (rx_ready_.empty() && tx_ready_.empty()) {
      absl::MutexLock lock(inbox_mu_);
      if (!stop_.load(std::memory_order_relaxed) && new_channels_.empty() &&
          woken_peers_.empty()) {
        sleeping_ = true;
        timeout_ms = -1;
      }
    }
    const int n = poller_->BlockingWait(events.data(), kMaxEvents, timeout_ms);
    if (stop_.load(std::memory_order_acquire)) return;

    // 2. Enqueue sockets that epoll reported as readable or writable.
    for (int i = 0; i < n; ++i) {
      const fd_t fd(events[i].data.fd);
      const uint32_t flags = events[i].events;

      if (fd == wake_fd_) {
        // One read resets the eventfd counter so epoll does not keep firing.
        uint64_t val = 0;
        if (ABSL_PREDICT_FALSE(::read(wake_fd_.value(), &val, sizeof(val)) <
                                   0 &&
                               errno != EAGAIN && errno != EINTR)) {
          LOG_EVERY_N_SEC(WARNING, 1)
              << "worker #" << id_ << " wake_fd read failed: errno=" << errno;
        }
        continue;
      }

      const auto it = sessions_.find(fd);
      if (it == sessions_.end()) continue;
      SessionEntry& entry = it->second;

      const bool readable = (flags & EPOLLIN) != 0;
      const bool became_writable = (flags & EPOLLOUT) != 0;
      const bool peer_closed = (flags & (EPOLLRDHUP | EPOLLHUP)) != 0;
      const bool fatal_error = (flags & (EPOLLERR | EPOLLHUP)) != 0;

      if (peer_closed) entry.peer_closed = true;
      if (fatal_error) entry.fatal_error = true;

      // Drain buffered RX (e.g., final ACKs/data) before closing on peer close.
      if (readable || peer_closed || entry.in_rx_ready) {
        scheduleRx(fd, entry);
      } else if (fatal_error) {
        closeSession(fd);
        continue;
      }

      // Socket send buffer has room again; resume paused TX.
      if (became_writable) {
        entry.session->ClearTxBlocked();
        scheduleTx(fd, entry);
      }
    }

    // 3. Clear sleeping_ (if we slept) and drain new channels / woken peers.
    if (timeout_ms < 0 || has_inbox_work_.load(std::memory_order_acquire)) {
      drainInbox();
    }

    // 4. Run one bounded round of reads, then writes, across queued sockets.
    driveReadyRx();
    driveReadyTx();
  }
}

void Worker::drainInbox() {
  static_assert(assumptions::kLocalIdentityIsReadOnlyAfterControlPlaneStarts);

  // 1. Swap inbox out under lock so epoll registration and lookups don't block
  //    app threads calling Wake() or AddChannel().
  std::vector<NewChannel> new_channels;
  absl::flat_hash_set<PeerId> woken_peers;
  {
    absl::MutexLock lock(inbox_mu_);
    sleeping_ = false;
    new_channels.swap(new_channels_);
    woken_peers.swap(woken_peers_);
    has_inbox_work_.store(false, std::memory_order_relaxed);
  }

  // 2. Register newly connected channels with epoll and schedule initial TX in
  //    case work was already queued before the channel became ready.
  for (NewChannel& item : new_channels) {
    const fd_t fd = item.channel->GetFd();
    if (ABSL_PREDICT_FALSE(fd.value() < 0)) {
      LOG(WARNING) << "worker #" << id_ << " rail " << rail_id_
                   << " ignoring channel without fd: " << *item.channel;
      continue;
    }
    if (ABSL_PREDICT_FALSE(poller_->Register(fd, kChannelEvents) < 0)) {
      LOG(WARNING) << "worker #" << id_ << " failed to register fd "
                   << fd.value();
      continue;
    }
    auto [it, inserted] = sessions_.emplace(
        fd, SessionEntry{
                .session = std::make_unique<TcpChannelSession>(
                    std::move(item.channel), self_.control_plane_listener,
                    item.peer, *item.work_queue, outgoing_, incoming_, id_),
            });
    if (inserted) {
      peer_fds_[item.peer].push_back(fd);
      scheduleTx(fd, it->second);
    }
  }

  // 3. Schedule TX only on channels serving peers that received new work.
  for (const PeerId& peer : woken_peers) {
    const auto pit = peer_fds_.find(peer);
    if (pit == peer_fds_.end()) continue;
    for (const fd_t fd : pit->second) {
      const auto sit = sessions_.find(fd);
      if (sit != sessions_.end()) scheduleTx(fd, sit->second);
    }
  }
}

void Worker::scheduleTx(const fd_t fd, SessionEntry& entry) {
  if (!entry.in_tx_ready && !entry.fatal_error &&
      !entry.session->IsTxBlocked()) {
    entry.in_tx_ready = true;
    tx_ready_.push_back(fd);
  }
}

void Worker::scheduleRx(const fd_t fd, SessionEntry& entry) {
  if (!entry.in_rx_ready) {
    entry.in_rx_ready = true;
    rx_ready_.push_back(fd);
  }
}

void Worker::driveReadyRx() {
  const size_t count = rx_ready_.size();
  for (size_t i = 0; i < count; ++i) {
    const fd_t fd = rx_ready_.front();
    rx_ready_.pop_front();

    const auto it = sessions_.find(fd);
    if (it == sessions_.end()) continue;
    SessionEntry& entry = it->second;
    entry.in_rx_ready = false;

    bool yielded = false;
    if (!entry.session->DriveRx(yielded, entry.peer_closed)) {
      closeSession(fd);
      continue;
    }
    if (yielded) {
      // More buffered RX bytes may remain; re-queue behind other ready fds.
      scheduleRx(fd, entry);
    } else if (ABSL_PREDICT_FALSE(entry.fatal_error)) {
      // Buffered RX is now drained; safe to tear down on hangup/error.
      closeSession(fd);
      continue;
    }
    // Flush ACKs/failure frames generated by RX on this same turn.
    if (entry.session->HasPendingControl()) {
      scheduleTx(fd, entry);
    }
  }
}

void Worker::driveReadyTx() {
  const size_t count = tx_ready_.size();
  for (size_t i = 0; i < count; ++i) {
    const fd_t fd = tx_ready_.front();
    tx_ready_.pop_front();

    const auto it = sessions_.find(fd);
    if (it == sessions_.end()) continue;
    SessionEntry& entry = it->second;
    entry.in_tx_ready = false;

    if (entry.fatal_error || entry.session->IsTxBlocked()) continue;
    bool yielded = false;
    if (!entry.session->DriveTx(yielded)) {
      closeSession(fd);
      continue;
    }
    if (yielded) {
      scheduleTx(fd, entry);
    }
  }
}

void Worker::closeSession(const fd_t fd) {
  poller_->Unregister(fd);
  const auto it = sessions_.find(fd);
  if (it == sessions_.end()) return;

  // Remove any queued entries so a closed fd cannot collide with a reused fd.
  // TODO(b/503756862): Avoid linear removal from rx_ready_/tx_ready_ if
  // mass-disconnect storms become a bottleneck (e.g., tag entries with a
  // monotonic session ID or use an intrusive list).
  if (it->second.in_rx_ready) {
    rx_ready_.erase(std::remove(rx_ready_.begin(), rx_ready_.end(), fd),
                    rx_ready_.end());
  }
  if (it->second.in_tx_ready) {
    tx_ready_.erase(std::remove(tx_ready_.begin(), tx_ready_.end(), fd),
                    tx_ready_.end());
  }

  const auto pit = peer_fds_.find(it->second.session->remote_peer());
  if (pit != peer_fds_.end()) {
    auto& fds = pit->second;
    fds.erase(std::remove(fds.begin(), fds.end(), fd), fds.end());
    if (fds.empty()) peer_fds_.erase(pit);
  }
  sessions_.erase(it);
}

}  // namespace peregrine::internal::engine2
