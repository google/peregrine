#ifndef PEREGRINE_SRC_INTERNAL_ENGINE_WORKER_H_
#define PEREGRINE_SRC_INTERNAL_ENGINE_WORKER_H_

#include <deque>
#include <memory>
#include <thread>  // NOLINT

#include "absl/base/thread_annotations.h"
#include "absl/synchronization/mutex.h"
#include "src/api/transport_types.h"
#include "src/internal/channel/channel.h"
#include "src/internal/chunk/chunk.h"
#include "src/internal/engine/transfer.h"
#include "src/internal/request/request_tracker.h"
#include "src/util/macro.h"

namespace peregrine::internal {

// This class implements a worker that sends and/or receives chunks over a
// set of channels.
// It is thread-compatible but not thread-safe.
class Worker {
 public:
  // Constructor.
  Worker(RequestTracker& outgoing, RequestTracker& incoming,
         std::unique_ptr<Channel> channel);

  // Disallows copy and assign.
  DISALLOW_COPY(Worker);
  DISALLOW_MOVE(Worker);

  // Destructor.
  ~Worker();

  // Enqueues a chunk to be sent later.
  void EnqueueChunk(const Byte* chunk_src_addr, const ChunkMetadata& chunk)
      ABSL_LOCKS_EXCLUDED(mu_);

  // Runs a loop to send chunks until the destructor is called.
  void SendLoop() ABSL_LOCKS_EXCLUDED(mu_);

  // Runs a loop to receive chunks until the destructor is called.
  void RecvLoop() ABSL_LOCKS_EXCLUDED(mu_);

 private:
  // Returns true iff there are pending chunks or the destructor is called.
  bool hasSendWork() const ABSL_EXCLUSIVE_LOCKS_REQUIRED(mu_);

 private:
  struct Entry {
    const Byte* chunk_src_addr;
    ChunkMetadata chunk;

    // Returns the chunk payload for sending.
    ChunkPayloadView GenPayload() const {
      return ChunkPayloadView(chunk_src_addr, chunk.size);
    }
  };

 private:
  mutable absl::Mutex mu_;
  bool stop_ ABSL_GUARDED_BY(mu_);
  std::deque<Entry> chunks_ ABSL_GUARDED_BY(mu_);

  Transfer xfer_;
  std::unique_ptr<Channel> channel_;

  std::jthread send_thread_;
  std::jthread recv_thread_;
};

}  // namespace peregrine::internal

#endif  // PEREGRINE_SRC_INTERNAL_ENGINE_WORKER_H_
