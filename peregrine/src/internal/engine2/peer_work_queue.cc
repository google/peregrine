#include "peregrine/src/internal/engine2/peer_work_queue.h"

#include <cstdint>
#include <deque>
#include <memory>
#include <optional>
#include <utility>
#include <vector>

#include "absl/log/check.h"
#include "absl/synchronization/mutex.h"
#include "peregrine/src/api/transport_types.h"

namespace peregrine::internal::engine2 {

void PeerWorkQueue::Enqueue(std::shared_ptr<QueuedPost> post) {
  DCHECK(post != nullptr);
  DCHECK(!post->req_infos.empty());
  DCHECK_EQ(post->post.requests.size(), post->req_infos.size());

  absl::MutexLock lock(mu_);
  queue_.push_back(Entry{.post = std::move(post)});
}

std::optional<ChunkClaim> PeerWorkQueue::TryPull() {
  absl::MutexLock lock(mu_);
  if (queue_.empty()) return std::nullopt;
  Entry& front = queue_.front();
  const uint32_t nchunks = front.post->req_infos[front.next_request].nchunks;
  ChunkClaim claim = {
      .post = front.post,
      .request_index = front.next_request,
      .chunk_index = front.next_chunk++,
  };
  if (front.next_chunk >= nchunks) {
    ++front.next_request;
    front.next_chunk = 0;
    if (front.next_request >= front.post->req_infos.size()) {
      queue_.pop_front();
    }
  }
  return claim;
}

void PeerWorkQueue::Purge(const Handle handle) {
  absl::MutexLock lock(mu_);
  std::erase_if(queue_, [handle](const Entry& entry) {
    return entry.post->post.handle == handle;
  });
}

std::vector<Handle> PeerWorkQueue::DrainHandles() {
  absl::MutexLock lock(mu_);
  std::vector<Handle> handles;
  handles.reserve(queue_.size());
  for (const Entry& entry : queue_) {
    handles.push_back(entry.post->post.handle);
  }
  queue_.clear();
  return handles;
}

}  // namespace peregrine::internal::engine2
