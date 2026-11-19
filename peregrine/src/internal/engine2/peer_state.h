#ifndef PEREGRINE_SRC_INTERNAL_ENGINE2_PEER_STATE_H_
#define PEREGRINE_SRC_INTERNAL_ENGINE2_PEER_STATE_H_

#include <atomic>
#include <cstdint>
#include <optional>

#include "peregrine/src/api/transport_types.h"
#include "peregrine/src/internal/engine2/post.h"
#include "peregrine/src/util/macro.h"

namespace peregrine::internal::engine2 {

// One chunk of one request, claimed by a worker to send. The worker turns the
// claim into buffers, so the claim is the same for any data layout.
struct ChunkClaim {
  const Post* post = nullptr;
  uint32_t request_index = 0;
  uint32_t chunk_index = 0;
};

// State of one peer.
//
// `PeerState` holds the work queued for a peer and whether the peer's host
// info has been requested.
//
// Thread-safety: This class is thread-safe.
class PeerState final {
 public:
  PeerState() = default;

  DISALLOW_COPY(PeerState);
  DISALLOW_MOVE(PeerState);

  ~PeerState() = default;

  // Queues `post` for transfer.
  //
  // Implementation details:
  // - Keeps each request whole, with a cursor marking how much of it has been
  //   handed out. Workers carve the next chunk off the front as they pull
  //   work, so enqueuing costs the same for any request size and faster
  //   channels naturally take more chunks.
  // - Queues one request frame for two-sided reads.
  void Enqueue(Post post);

  // Drops all queued work for `handle`.
  //
  // Implementation details:
  // - Removes `handle`'s queued requests, frames, and chunks awaiting retry.
  //   A chunk already pulled by a worker is not affected.
  void Purge(Handle handle);

  // Claims the next chunk of the oldest queued request for a worker to send,
  // or returns nullopt if no work is queued. The claimed post stays valid
  // until it completes or is cancelled.
  //
  // Implementation details:
  // - Advances the request's chunk cursor; moves to the next request once
  //   every chunk of this one is claimed.
  std::optional<ChunkClaim> TryPull();

  // Returns true iff the caller should get the peer's host info: no request
  // for it has started since this object was created or since the last
  // `AbortPosts()`. Lock-free; after the first call this is a single load.
  bool ShouldGetPeerHostInfo();

  // Aborts all queued posts after a setup failure. The next
  // `ShouldGetPeerHostInfo()` returns true, so the next post gets the peer's
  // host info again.
  //
  // Implementation details:
  // - Drops the queued posts and reports each one as failed to the outgoing
  //   tracker, which reports the failure to the app. Resets the flag while
  //   holding the queue's lock, so a post queued after the drop always finds
  //   it reset.
  void AbortPosts();

 private:
  std::atomic<bool> host_info_requested_ = false;
};

}  // namespace peregrine::internal::engine2

#endif  // PEREGRINE_SRC_INTERNAL_ENGINE2_PEER_STATE_H_
