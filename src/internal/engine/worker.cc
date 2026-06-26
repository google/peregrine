#include "src/internal/engine/worker.h"

#include <memory>
#include <string_view>
#include <thread>  // NOLINT
#include <utility>

#include "absl/log/check.h"
#include "absl/log/log.h"
#include "absl/synchronization/mutex.h"
#include "src/api/transport_types.h"
#include "src/internal/buffer/buffer_tracker.h"
#include "src/internal/channel/channel.h"
#include "src/internal/chunk/chunk.h"

namespace peregrine::internal {

constexpr std::string_view kWorker = "worker ";

Worker::Worker(BufferTracker& send, BufferTracker& recv,
               std::unique_ptr<Channel> channel)
    : stop_(false), chunks_(), xfer_(send, recv), channel_(std::move(channel)) {
  DCHECK_NE(channel_, nullptr);
  send_thread_ = std::jthread([this]() { SendLoop(); });
  recv_thread_ = std::jthread([this]() { RecvLoop(); });
  LOG(INFO) << kWorker << "created @ " << this;
}

Worker::~Worker() {
  {
    absl::MutexLock _(mu_);
    stop_ = true;
  }
  // all threads are joined in their destructor.
  LOG(INFO) << kWorker << "destroyed @ " << this;
}

void Worker::SendChunk(const Byte* const chunk_src_addr,
                       const ChunkMetadata& chunk) {
  absl::MutexLock _(mu_);
  chunks_.push({chunk_src_addr, chunk});
}

bool Worker::hasSendWork() const { return !chunks_.empty() || stop_; }

void Worker::SendLoop() {
  LOG(INFO) << kWorker << "send loop @ " << this;
  while (true) {
    Entry entry;
    {
      absl::MutexLock _(mu_);
      mu_.Await(absl::Condition(this, &Worker::hasSendWork));
      if (chunks_.empty()) {
        channel_->Close();
        LOG(INFO) << kWorker << "send loop stopped @ " << this;
        DCHECK(stop_);
        return;
      }
      entry = std::move(chunks_.front());
      chunks_.pop();
    }

    const ChunkMetadata& chunk = entry.chunk;
    DCHECK(chunk.IsValid());
    const auto payload = entry.GenPayload();
    if (!xfer_.SendChunk(channel_.get(), chunk, payload)) {
      // TODO(yongx): Handle errors.
      LOG(WARNING) << kWorker << "send chunk failed @ " << this;
      break;
    }
  }
}

void Worker::RecvLoop() {
  LOG(INFO) << kWorker << "recv loop @ " << this;
  while (true) {
    {
      absl::MutexLock _(mu_);
      if (stop_) {
        channel_->Close();
        LOG(INFO) << kWorker << "recv loop stopped @ " << this;
        return;
      }
    }
    if (!xfer_.RecvChunk(channel_.get())) {
      // TODO(yongx): Handle errors.
      LOG(WARNING) << kWorker << "recv chunk failed @ " << this;
      break;
    }
  }
}

}  // namespace peregrine::internal
