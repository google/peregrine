#ifndef PEREGRINE_SRC_INTERNAL_ENGINE2_WORKER_H_
#define PEREGRINE_SRC_INTERNAL_ENGINE2_WORKER_H_

#include <cstdint>
#include <memory>

#include "peregrine/src/internal/channel/channel.h"
#include "peregrine/src/internal/engine2/peer_state.h"
#include "peregrine/src/util/macro.h"

namespace peregrine::internal::engine2 {

// Moves data over the channels of one local rail.
//
// `Worker` serves the channels on one local rail. On channels it opened, it
// pulls chunks from the peer's queue and writes them. On channels accepted
// from peers, it reads incoming frames and chunks.
//
// Thread-safety: This class is thread-safe.
class Worker final {
 public:
  // Creates the worker for local rail `rail_id`.
  //
  // Implementation details:
  // - Starts a thread pinned to a CPU on the rail's NUMA node, running an
  //   event loop over the worker's channels.
  explicit Worker(uint32_t rail_id);

  DISALLOW_COPY(Worker);
  DISALLOW_MOVE(Worker);

  // Implementation details:
  // - Stops the thread and closes the worker's channels.
  ~Worker() = default;

  // Takes `ch`, a channel this host opened to the peer whose state is
  // `peer_state`, and sends that peer's queued work on it. `peer_state` must
  // outlive the worker.
  //
  // Implementation details:
  // - Adds `ch` to the event loop. When `ch` is writable, claims chunks with
  //   `peer_state.TryPull()` and writes them, after the post's info frame if
  //   `ch` has not carried that post yet.
  void AddOutboundChannel(std::unique_ptr<Channel> ch, PeerState& peer_state);

  // Takes `ch`, a channel accepted from a remote peer, and reads from it.
  //
  // Implementation details:
  // - Adds `ch` to the event loop. Learns the peer from the first post info
  //   frame, places incoming chunks into their destination buffers, and acks
  //   them.
  void AddInboundChannel(std::unique_ptr<Channel> ch);

  // Tells the worker that a peer it serves may have new work.
  //
  // Implementation details:
  // - Wakes the event loop, which retries pulling on idle outbound channels.
  void Wake();

 private:
  const uint32_t rail_id_;
};

}  // namespace peregrine::internal::engine2

#endif  // PEREGRINE_SRC_INTERNAL_ENGINE2_WORKER_H_
