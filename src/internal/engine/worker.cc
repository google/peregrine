#include "src/internal/engine/worker.h"

#include <memory>
#include <string_view>
#include <thread>  // NOLINT
#include <utility>

#include "absl/log/check.h"
#include "absl/log/log.h"
#include "absl/synchronization/mutex.h"
#include "src/api/transport_types.h"
#include "src/internal/base/hostinfo.h"
#include "src/internal/channel/channel.h"
#include "src/internal/chunk/chunk.h"
#include "src/internal/request/request_tracker.h"
#include "src/internal/transfer/transfer.h"

namespace peregrine::internal {

void Worker::log(std::string_view msg) const {
  LOG(INFO) << "worker #" << id_ << " " << msg << " @ " << self_;
}

Worker::Worker(int id, const HostInfo& self, RequestTracker& outgoing,
               RequestTracker& incoming, std::unique_ptr<Channel> channel)
    : id_(id),
      self_(self),
      stop_(false),
      chunks_(),
      outgoing_(outgoing),
      incoming_(incoming),
      channel_(std::move(channel)) {
  DCHECK_NE(channel_, nullptr);
  send_thread_ = std::jthread([this]() { SendLoop(); });
  recv_thread_ = std::jthread([this]() { RecvLoop(); });
  log("created");
}

Worker::~Worker() {
  {
    absl::MutexLock _(mu_);
    stop_ = true;
    channel_->Shutdown();
  }
  // all threads are joined in their destructor.
  log("destroyed");
}

void Worker::EnqueueChunk(const Byte* const chunk_src_addr,
                          const ChunkMetadata& chunk) {
  DCHECK_NE(chunk_src_addr, nullptr);
  DCHECK(chunk.IsValid());
  DCHECK(!chunk.IsAck());

  absl::MutexLock _(mu_);
  chunks_.emplace_back(chunk_src_addr, chunk);
}

bool Worker::hasSendWork() const { return !chunks_.empty() || stop_; }

void Worker::SendLoop() {
  log("send loop started");
  while (true) {
    Entry entry;
    {
      absl::MutexLock _(mu_);
      mu_.Await(absl::Condition(this, &Worker::hasSendWork));
      if (chunks_.empty()) {
        log("send loop stopped");
        DCHECK(stop_);
        return;
      }
      entry = std::move(chunks_.front());
      chunks_.pop_front();
    }

    const ChunkMetadata& chunk = entry.chunk;
    DCHECK(chunk.IsValid());
    const auto payload = entry.GenPayload();
    if (!Transfer::SendChunk(channel_.get(), chunk, payload)) {
      // TODO(yongx): Handle errors.
      log("send chunk failed");
      break;
    }
  }
}

void Worker::RecvLoop() {
  log("recv loop started");
  while (true) {
    {
      absl::MutexLock _(mu_);
      if (stop_) {
        log("recv loop stopped");
        return;
      }
    }
    if (!Transfer::RecvChunk(channel_.get(), outgoing_, incoming_)) {
      // TODO(yongx): Handle errors.
      log("recv chunk failed");
      break;
    }
  }
}

}  // namespace peregrine::internal
