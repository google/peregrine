#include "peregrine/src/internal/engine2/worker.h"

#include <sys/epoll.h>
#include <sys/eventfd.h>
#include <sys/types.h>
#include <unistd.h>

#include <array>
#include <atomic>
#include <cstdint>
#include <memory>
#include <utility>
#include <vector>

#include "absl/base/optimization.h"
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
  stop_.store(true, std::memory_order_release);
  signalWake();
  if (thread_.joinable()) thread_.join();
  sessions_.clear();
  if (wake_fd_.value() >= 0) ::close(wake_fd_.value());
}

void Worker::AddChannel(const PeerId peer, std::unique_ptr<Channel> ch,
                        PeerWorkQueue& work_queue) {
  if (ABSL_PREDICT_FALSE(ch == nullptr)) return;
  {
    absl::MutexLock lock(pending_channels_mu_);
    pending_channels_.push_back(PendingChannel{
        .peer = peer,
        .channel = std::move(ch),
        .work_queue = &work_queue,
    });
    has_pending_channels_.store(true, std::memory_order_release);
  }
  Wake();
}

void Worker::Wake() {
  if (!wake_pending_.exchange(true, std::memory_order_seq_cst)) {
    signalWake();
  }
}

void Worker::signalWake() {
  const uint64_t one = 1;
  [[maybe_unused]] const ssize_t n =
      ::write(wake_fd_.value(), &one, sizeof(one));
}

void Worker::run() {
  std::array<epoll_event, kMaxEvents> events = {};
  // True if a session yielded at its write budget and needs another turn.
  bool tx_pending = false;

  while (!stop_.load(std::memory_order_acquire)) {
    const int timeout_ms = tx_pending ? 0 : -1;
    const int n = poller_->BlockingWait(events.data(), kMaxEvents, timeout_ms);
    if (stop_.load(std::memory_order_acquire)) return;

    // Sweep all sessions at the end of this turn if woken, new channels arrive,
    // or the previous turn left pending TX work.
    bool sweep_tx = tx_pending;
    tx_pending = false;

    for (int i = 0; i < n; ++i) {
      const fd_t fd(events[i].data.fd);
      const uint32_t flags = events[i].events;

      // 1. Cross-thread wakeup (new work queued or new channel pending).
      if (fd == wake_fd_) {
        uint64_t val = 0;
        while (::read(wake_fd_.value(), &val, sizeof(val)) > 0) {
        }
        wake_pending_.store(false, std::memory_order_seq_cst);
        sweep_tx = true;
        continue;
      }

      const auto it = sessions_.find(fd);
      if (it == sessions_.end()) continue;
      TcpChannelSession& session = *it->second;

      const bool readable = (flags & EPOLLIN) != 0;
      const bool became_writable = (flags & EPOLLOUT) != 0;
      const bool peer_closed = (flags & (EPOLLRDHUP | EPOLLHUP)) != 0;
      const bool fatal_error = (flags & (EPOLLERR | EPOLLHUP)) != 0;

      // 2. Drain RX before closing on hangup/error so buffered ACKs/data are
      //    not lost if the peer closed right after sending its final frame.
      if (readable || peer_closed) {
        if (!session.DriveRx(peer_closed)) {
          closeSession(fd);
          continue;
        }
      }
      if (ABSL_PREDICT_FALSE(fatal_error)) {
        closeSession(fd);
        continue;
      }

      // 3. Resume TX if EPOLLOUT fired, or flush control frames queued by RX.
      if (became_writable) session.ClearTxBlocked();
      const bool should_send =
          became_writable ||
          (session.HasPendingControl() && !session.IsTxBlocked());
      if (should_send) {
        bool yielded = false;
        if (!session.DriveTx(yielded)) {
          closeSession(fd);
          continue;
        }
        if (yielded) tx_pending = true;
      }
    }

    // 4. Admit any newly handed-off channels, then sweep TX if needed.
    if (has_pending_channels_.load(std::memory_order_acquire) &&
        admitPendingChannels()) {
      sweep_tx = true;
    }
    if (sweep_tx && driveAllTx()) {
      tx_pending = true;
    }
  }
}

bool Worker::admitPendingChannels() {
  static_assert(assumptions::kLocalIdentityIsReadOnlyAfterControlPlaneStarts);
  // Swap out queued channels under lock so epoll registration runs lock-free.
  std::vector<PendingChannel> items;
  {
    absl::MutexLock lock(pending_channels_mu_);
    items.swap(pending_channels_);
    has_pending_channels_.store(false, std::memory_order_release);
  }
  bool added = false;
  for (PendingChannel& item : items) {
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
    sessions_.emplace(
        fd, std::make_unique<TcpChannelSession>(
                std::move(item.channel), self_.control_plane_listener,
                item.peer, *item.work_queue, outgoing_, incoming_, id_));
    added = true;
  }
  return added;
}

bool Worker::driveAllTx() {
  // Drive each non-blocked session; defer erasures until after map iteration.
  bool any_yielded = false;
  std::vector<fd_t> failed;
  for (auto& [fd, session] : sessions_) {
    if (session->IsTxBlocked()) continue;
    bool yielded = false;
    if (!session->DriveTx(yielded)) {
      failed.push_back(fd);
    } else if (yielded) {
      any_yielded = true;
    }
  }
  for (const fd_t fd : failed) {
    closeSession(fd);
  }
  return any_yielded;
}

void Worker::closeSession(const fd_t fd) {
  poller_->Unregister(fd);
  sessions_.erase(fd);
}

}  // namespace peregrine::internal::engine2
