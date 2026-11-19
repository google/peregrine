#include "peregrine/src/internal/engine2/peer_state.h"

#include <atomic>

#include "peregrine/src/api/transport_types.h"
#include "peregrine/src/internal/engine2/assumptions.h"
#include "peregrine/src/internal/engine2/post.h"

namespace peregrine::internal::engine2 {

void PeerState::Enqueue(Post post) {}

void PeerState::Purge(Handle handle) {}

bool PeerState::ShouldGetPeerHostInfo() {
  if (host_info_requested_.load(std::memory_order_acquire)) return false;
  return !host_info_requested_.exchange(true, std::memory_order_acq_rel);
}

void PeerState::AbortPosts() {
  static_assert(assumptions::kSetupFailuresAbortQueuedPosts);
  host_info_requested_.store(false, std::memory_order_release);
}

}  // namespace peregrine::internal::engine2
