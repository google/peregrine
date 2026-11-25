#ifndef PEREGRINE_SRC_INTERNAL_ENGINE2_WORKER_H_
#define PEREGRINE_SRC_INTERNAL_ENGINE2_WORKER_H_

#include <atomic>
#include <cstdint>
#include <deque>
#include <memory>
#include <vector>

#include "absl/base/thread_annotations.h"
#include "absl/container/flat_hash_map.h"
#include "absl/container/flat_hash_set.h"
#include "absl/synchronization/mutex.h"
#include "peregrine/src/internal/base/endpoint.h"
#include "peregrine/src/internal/base/hostinfo.h"
#include "peregrine/src/internal/base/types.h"
#include "peregrine/src/internal/channel/channel.h"
#include "peregrine/src/internal/engine2/peer_work_queue.h"
#include "peregrine/src/internal/engine2/tcp_channel_session.h"
#include "peregrine/src/internal/event/poller.h"
#include "peregrine/src/internal/request/request_tracker.h"
#include "peregrine/src/internal/trace/trace_recorder.h"
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
  // `incoming`, and `tracer` must outlive the worker.
  Worker(int id, int32_t rail_id, const HostInfo& self,
         RequestTracker& outgoing, RequestTracker& incoming,
         TraceRecorder& tracer);

  DISALLOW_COPY(Worker);
  DISALLOW_MOVE(Worker);

  // Stops the worker thread and closes all assigned channels.
  ~Worker();

  // Hands `ch` for `peer` (backed by `work_queue`, which must outlive the
  // worker) to the worker's event loop and wakes it.
  void AddChannel(PeerId peer, std::unique_ptr<Channel> ch,
                  PeerWorkQueue& work_queue) ABSL_LOCKS_EXCLUDED(inbox_mu_);

  // Signals the worker that `peer` has new outgoing work to send.
  void Wake(const PeerId& peer) ABSL_LOCKS_EXCLUDED(inbox_mu_);

 private:
  // Newly connected channel waiting to be registered on the worker thread.
  struct NewChannel {
    PeerId peer;
    std::unique_ptr<Channel> channel;
    PeerWorkQueue* work_queue = nullptr;
  };

  // Per-channel state owned exclusively by the worker thread.
  struct SessionEntry {
    std::unique_ptr<TcpChannelSession> session;
    bool in_tx_ready = false;
    bool in_rx_ready = false;
    bool peer_closed = false;
    bool fatal_error = false;
  };

  // Wakes the event loop immediately.
  void signalWake();

  // Runs the worker's event loop until stopped.
  void run();

  // Registers newly handed-off channels and schedules TX for woken peers.
  void drainInbox() ABSL_LOCKS_EXCLUDED(inbox_mu_);

  // Adds `fd` to the TX/RX runnable queue if not already queued.
  void scheduleTx(fd_t fd, SessionEntry& entry);
  void scheduleRx(fd_t fd, SessionEntry& entry);

  // Drives one fair round of queued RX / TX sessions.
  void driveReadyRx();
  void driveReadyTx();

  // Unregisters and destroys the session for `fd`.
  void closeSession(fd_t fd);

 private:
  const int id_;
  const int32_t rail_id_;
  const fd_t wake_fd_;

  // Non-owning, thread-safe references; all must outlive the worker.
  const HostInfo& self_;
  RequestTracker& outgoing_;
  RequestTracker& incoming_;
  TraceRecorder& tracer_;

  // Owned event loop and cross-thread wakeup state.
  std::unique_ptr<Poller> poller_;
  std::atomic<bool> stop_{false};
  // Fast-path flag so the busy loop skips `inbox_mu_` when nothing was posted.
  std::atomic<bool> has_inbox_work_{false};

  // Cross-thread inbox and sleep state. `Wake()` / `AddChannel()` only write to
  // `wake_fd_` when they transition `sleeping_` from true to false.
  absl::Mutex inbox_mu_;
  bool sleeping_ ABSL_GUARDED_BY(inbox_mu_) = false;
  std::vector<NewChannel> new_channels_ ABSL_GUARDED_BY(inbox_mu_);
  absl::flat_hash_set<PeerId> woken_peers_ ABSL_GUARDED_BY(inbox_mu_);

  // Owned and accessed exclusively on the worker thread (torn down after join).
  absl::flat_hash_map<fd_t, SessionEntry> sessions_;
  // Reverse index mapping each remote peer to the fds serving it on this
  // worker.
  absl::flat_hash_map<PeerId, std::vector<fd_t>> peer_fds_;
  std::deque<fd_t> tx_ready_;
  std::deque<fd_t> rx_ready_;

  util::Jthread thread_;
};

}  // namespace peregrine::internal::engine2

#endif  // PEREGRINE_SRC_INTERNAL_ENGINE2_WORKER_H_
