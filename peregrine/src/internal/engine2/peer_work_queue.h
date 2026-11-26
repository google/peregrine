#ifndef PEREGRINE_SRC_INTERNAL_ENGINE2_PEER_WORK_QUEUE_H_
#define PEREGRINE_SRC_INTERNAL_ENGINE2_PEER_WORK_QUEUE_H_

#include <cstddef>
#include <cstdint>
#include <deque>
#include <memory>
#include <optional>
#include <vector>

#include "absl/base/thread_annotations.h"
#include "absl/container/inlined_vector.h"
#include "absl/synchronization/mutex.h"
#include "peregrine/src/api/transport_types.h"
#include "peregrine/src/internal/base/types.h"
#include "peregrine/src/internal/chunk/header.h"
#include "peregrine/src/internal/chunk/header_flatbuf.h"
#include "peregrine/src/internal/engine2/post.h"
#include "peregrine/src/util/macro.h"

namespace peregrine::internal::engine2 {

// Fixed chunk size used to slice requests into work items.
inline constexpr size_t kChunkSize = 512 * 1024;

// A post queued for transfer along with its request descriptors.
struct QueuedPost {
  Post post;
  std::vector<RequestInfo> req_infos;
  // Pre-serialized post-info frame shared read-only across channels; inline
  // capacity covers up to 4-request posts without extra heap allocation.
  static constexpr size_t kInlinePostInfoBytes =
      HeaderUtil::kSize + 4 * HeaderUtil::kRequestInfoSize;
  absl::InlinedVector<Byte, kInlinePostInfoBytes> post_info;
};

// One chunk of one request, claimed by a worker to send.
struct ChunkClaim {
  std::shared_ptr<const QueuedPost> post;
  size_t request_index = 0;
  uint32_t chunk_index = 0;
};

// Per-peer queue of outgoing posts sliced into chunks for workers to pull.
//
// Thread-safety: This class is thread-safe.
class PeerWorkQueue final {
 public:
  PeerWorkQueue() = default;

  DISALLOW_COPY(PeerWorkQueue);
  DISALLOW_MOVE(PeerWorkQueue);

  ~PeerWorkQueue() = default;

  // Appends `post` to the queue.
  void Enqueue(std::shared_ptr<QueuedPost> post) ABSL_LOCKS_EXCLUDED(mu_);

  // Claims the next chunk of the oldest queued post, or returns nullopt if the
  // queue is empty.
  std::optional<ChunkClaim> TryPull() ABSL_LOCKS_EXCLUDED(mu_);

  // Drops all queued posts matching `handle`. Already-claimed chunks are not
  // affected.
  void Purge(Handle handle) ABSL_LOCKS_EXCLUDED(mu_);

  // Removes all queued posts and returns their handles.
  std::vector<Handle> DrainHandles() ABSL_LOCKS_EXCLUDED(mu_);

 private:
  // Queued post paired with the cursor of the next unclaimed chunk.
  struct Entry {
    std::shared_ptr<QueuedPost> post;
    size_t next_request = 0;
    uint32_t next_chunk = 0;
  };

 private:
  absl::Mutex mu_;
  std::deque<Entry> queue_ ABSL_GUARDED_BY(mu_);
};

}  // namespace peregrine::internal::engine2

#endif  // PEREGRINE_SRC_INTERNAL_ENGINE2_PEER_WORK_QUEUE_H_
