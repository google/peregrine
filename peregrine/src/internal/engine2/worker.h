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

  // Takes `ch` for the peer whose state is `peer_state`, and uses it for both
  // sending that peer's queued work and receiving incoming data. `peer_state`
  // must outlive the worker.
  //
  // Implementation details:
  // - Adds `ch` to the event loop. When writable, claims chunks with
  //   `peer_state.TryPull()` and writes them. When readable, places incoming
  //   chunks into their destination buffers and acks them.
  void AddChannel(std::unique_ptr<Channel> ch, PeerState& peer_state);

  // Tells the worker that a peer it serves may have new work.
  //
  // Implementation details:
  // - Wakes the event loop, which retries pulling on idle channels.
  void Wake();

 private:
  const uint32_t rail_id_;
};

}  // namespace peregrine::internal::engine2

#endif  // PEREGRINE_SRC_INTERNAL_ENGINE2_WORKER_H_
