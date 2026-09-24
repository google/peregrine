#ifndef PEREGRINE_SRC_INTERNAL_ENGINE_WORKER_H_
#define PEREGRINE_SRC_INTERNAL_ENGINE_WORKER_H_

#include <deque>
#include <memory>
#include <string_view>

#include "absl/base/thread_annotations.h"
#include "absl/synchronization/mutex.h"
#include "peregrine/src/api/transport_types.h"
#include "peregrine/src/internal/base/hostinfo.h"
#include "peregrine/src/internal/channel/channel.h"
#include "peregrine/src/internal/chunk/chunk.h"
#include "peregrine/src/internal/metrics/engine_metrics.h"
#include "peregrine/src/internal/request/request_tracker.h"
#include "peregrine/src/util/macro.h"
#include "peregrine/src/util/thread.h"

namespace peregrine::internal {

// This class implements a worker that sends and/or receives chunks over a
// set of channels.
// It is thread-compatible but not thread-safe.
class Worker {
 public:
  // Constructor.
  Worker(int id, const HostInfo& self, RequestTracker& outgoing,
         RequestTracker& incoming, EngineMetrics& metrics,
         std::unique_ptr<Channel> channel);

  // Disallows copy and assign.
  DISALLOW_COPY(Worker);
  DISALLOW_MOVE(Worker);

  // Destructor.
  ~Worker();

  // Enqueues a chunk to be sent later.
  void EnqueueChunk(const Byte* chunk_src_addr, const ChunkHeader& chunk)
      ABSL_LOCKS_EXCLUDED(mu_);

  // Runs a loop to send chunks until the destructor is called.
  void SendLoop() ABSL_LOCKS_EXCLUDED(mu_);

  // Runs a loop to receive chunks until the destructor is called.
  void RecvLoop() ABSL_LOCKS_EXCLUDED(mu_);

 private:
  // Returns true iff there are pending chunks or the destructor is called.
  bool hasSendWork() const ABSL_EXCLUSIVE_LOCKS_REQUIRED(mu_);

  // Logs a message.
  void log(std::string_view msg) const;

 private:
  struct Entry {
    const Byte* chunk_src_addr;
    ChunkHeader chunk;

    // Returns the chunk payload for sending.
    ChunkPayloadView GenPayload() const {
      return ChunkPayloadView(chunk_src_addr, chunk.size);
    }
  };

 private:
  const int id_;
  const HostInfo self_;

  mutable absl::Mutex mu_;
  bool stop_ ABSL_GUARDED_BY(mu_);
  std::deque<Entry> chunks_ ABSL_GUARDED_BY(mu_);

  RequestTracker& outgoing_;
  RequestTracker& incoming_;

  EngineMetrics& metrics_;

  std::unique_ptr<Channel> channel_;

  util::Jthread send_thread_;
  util::Jthread recv_thread_;
};

}  // namespace peregrine::internal

#endif  // PEREGRINE_SRC_INTERNAL_ENGINE_WORKER_H_
