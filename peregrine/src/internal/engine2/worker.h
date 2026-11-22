#ifndef PEREGRINE_SRC_INTERNAL_ENGINE2_WORKER_H_
#define PEREGRINE_SRC_INTERNAL_ENGINE2_WORKER_H_

#include <atomic>
#include <cstdint>
#include <memory>
#include <vector>

#include "absl/base/thread_annotations.h"
#include "absl/container/flat_hash_map.h"
#include "absl/synchronization/mutex.h"
#include "peregrine/src/internal/base/endpoint.h"
#include "peregrine/src/internal/base/hostinfo.h"
#include "peregrine/src/internal/base/types.h"
#include "peregrine/src/internal/channel/channel.h"
#include "peregrine/src/internal/engine2/peer_work_queue.h"
#include "peregrine/src/internal/engine2/tcp_channel_session.h"
#include "peregrine/src/internal/event/poller.h"
#include "peregrine/src/internal/request/request_tracker.h"
#include "peregrine/src/util/macro.h"
#include "peregrine/src/util/thread.h"

namespace peregrine::internal::engine2 {

// Drives non-blocking I/O over the channels of one local rail on a dedicated
// epoll thread.
//
// Thread-safety: This class is thread-safe.
class Worker final {
 public:
  // Creates and starts the worker for local rail `rail_id`. `self`, `outgoing`,
  // and `incoming` must outlive the worker.
  Worker(int id, uint32_t rail_id, const HostInfo& self,
         RequestTracker& outgoing, RequestTracker& incoming);

  DISALLOW_COPY(Worker);
  DISALLOW_MOVE(Worker);

  // Stops the worker thread and closes all assigned channels.
  ~Worker();

  // Hands `ch` for `peer` (backed by `work_queue`, which must outlive the
  // worker) to the worker's event loop and wakes it.
  void AddChannel(PeerId peer, std::unique_ptr<Channel> ch,
                  PeerWorkQueue& work_queue)
      ABSL_LOCKS_EXCLUDED(pending_channels_mu_);

  // Signals the worker that a peer it serves may have new outgoing work.
  void Wake();

 private:
  // Newly connected channel waiting to be admitted on the worker thread.
  struct PendingChannel {
    PeerId peer;
    std::unique_ptr<Channel> channel;
    PeerWorkQueue* work_queue = nullptr;
  };

  // Wakes the event loop immediately.
  void signalWake();

  // Runs the worker's event loop until stopped.
  void run();

  // Admits newly handed-off channels and registers them for I/O events.
  bool admitPendingChannels() ABSL_LOCKS_EXCLUDED(pending_channels_mu_);

  // Drives outgoing sends on all writable sessions; returns true if any
  // session still has more work to send.
  bool driveAllTx();

  // Unregisters and destroys the session for `fd`.
  void closeSession(fd_t fd);

 private:
  const int id_;
  const uint32_t rail_id_;
  const fd_t wake_fd_;

  // Non-owning, thread-safe references; all must outlive the worker.
  const HostInfo& self_;
  RequestTracker& outgoing_;
  RequestTracker& incoming_;

  // Owned event loop and cross-thread wakeup state.
  std::unique_ptr<Poller> poller_;
  std::atomic<bool> stop_{false};
  // Coalesces concurrent `Wake()` calls into a single eventfd write.
  std::atomic<bool> wake_pending_{false};
  // Fast-path flag avoiding `pending_channels_mu_` when no channels await.
  std::atomic<bool> has_pending_channels_{false};

  absl::Mutex pending_channels_mu_;
  std::vector<PendingChannel> pending_channels_
      ABSL_GUARDED_BY(pending_channels_mu_);

  // Owned and accessed exclusively on the worker thread (torn down after join).
  absl::flat_hash_map<fd_t, std::unique_ptr<TcpChannelSession>> sessions_;

  util::Jthread thread_;
};

}  // namespace peregrine::internal::engine2

#endif  // PEREGRINE_SRC_INTERNAL_ENGINE2_WORKER_H_
