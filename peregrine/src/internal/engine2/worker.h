#ifndef PEREGRINE_SRC_INTERNAL_ENGINE2_WORKER_H_
#define PEREGRINE_SRC_INTERNAL_ENGINE2_WORKER_H_

#include <atomic>
#include <cstdint>
#include <deque>
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
  Worker(int id, int32_t rail_id, const HostInfo& self,
         RequestTracker& outgoing, RequestTracker& incoming);

  DISALLOW_COPY(Worker);
  DISALLOW_MOVE(Worker);

  // Stops the worker thread and closes all assigned channels.
  ~Worker();

  // Hands `ch` for `peer` (backed by `work_queue`, which must outlive the
  // worker) to the worker's event loop and wakes it.
  void AddChannel(PeerId peer, std::unique_ptr<Channel> ch,
                  PeerWorkQueue& work_queue)
      ABSL_LOCKS_EXCLUDED(new_channels_mu_);

  // Signals the worker that a peer it serves may have new outgoing work.
  void Wake();

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

  // Registers newly handed-off channels and schedules them for TX.
  void registerNewChannels() ABSL_LOCKS_EXCLUDED(new_channels_mu_);

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

  // Owned event loop and cross-thread wakeup state.
  std::unique_ptr<Poller> poller_;
  std::atomic<bool> stop_{false};
  // Coalesces concurrent `Wake()` calls into a single eventfd write.
  std::atomic<bool> wake_pending_{false};
  // Fast-path flag avoiding `new_channels_mu_` when no channels await.
  std::atomic<bool> has_new_channels_{false};

  absl::Mutex new_channels_mu_;
  std::vector<NewChannel> new_channels_ ABSL_GUARDED_BY(new_channels_mu_);

  // Owned and accessed exclusively on the worker thread (torn down after join).
  absl::flat_hash_map<fd_t, SessionEntry> sessions_;
  std::deque<fd_t> tx_ready_;
  std::deque<fd_t> rx_ready_;

  util::Jthread thread_;
};

}  // namespace peregrine::internal::engine2

#endif  // PEREGRINE_SRC_INTERNAL_ENGINE2_WORKER_H_
