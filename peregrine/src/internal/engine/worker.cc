#include "peregrine/src/internal/engine/worker.h"

#include <memory>
#include <string_view>
#include <utility>
#include <vector>

#include "absl/log/check.h"
#include "absl/log/log.h"
#include "absl/synchronization/mutex.h"
#include "absl/types/span.h"
#include "peregrine/src/api/transport_types.h"
#include "peregrine/src/internal/assumptions.h"
#include "peregrine/src/internal/base/hostinfo.h"
#include "peregrine/src/internal/channel/channel.h"
#include "peregrine/src/internal/channel/channel_type.h"
#include "peregrine/src/internal/chunk/header.h"
#include "peregrine/src/internal/metrics/engine_metrics.h"
#include "peregrine/src/internal/request/request_tracker.h"
#include "peregrine/src/internal/transfer/transfer.h"
#include "peregrine/src/util/thread.h"

namespace peregrine::internal {

void Worker::log(std::string_view msg) const {
  LOG(INFO) << "worker #" << id_ << " " << msg << " @ " << self_;
}

Worker::Worker(int id, const HostInfo& self, RequestTracker& outgoing,
               RequestTracker& incoming, EngineMetrics& metrics,
               std::unique_ptr<Channel> channel)
    : id_(id),
      self_(self),
      stop_(false),
      infos_(),
      chunks_(),
      outgoing_(outgoing),
      incoming_(incoming),
      metrics_(metrics),
      channel_(std::move(channel)) {
  DCHECK_NE(channel_, nullptr);
  send_thread_ = util::Jthread([this]() { SendLoop(); });
  recv_thread_ = util::Jthread([this]() { RecvLoop(); });
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

void Worker::EnqueueInfo(const PostInfoHeader& info,
                         absl::Span<const RequestInfo> reqs) {
  DCHECK(info.IsValid());
  DCHECK(!reqs.empty());
  if (channel_->Type().IsOneSidedChannel()) return;

  absl::MutexLock _(mu_);
  infos_.push_back(
      InfoEntry{info, std::vector<RequestInfo>(reqs.begin(), reqs.end())});
}

void Worker::EnqueueChunk(const Byte* const chunk_src_addr,
                          const DataHeader& chunk) {
  DCHECK_NE(chunk_src_addr, nullptr);
  DCHECK(chunk.IsData());

  absl::MutexLock _(mu_);
  chunks_.push_back(ChunkEntry{chunk_src_addr, chunk});
}

bool Worker::hasSendWork() const {
  return !infos_.empty() || !chunks_.empty() || stop_;
}

void Worker::SendLoop() {
  log("send loop started");
  while (true) {
    bool is_info = false;
    InfoEntry info_entry;
    ChunkEntry chunk_entry;
    {
      absl::MutexLock _(mu_);
      mu_.Await(absl::Condition(this, &Worker::hasSendWork));
      if (!infos_.empty()) {
        is_info = true;
        info_entry = std::move(infos_.front());
        infos_.pop_front();
      } else if (!chunks_.empty()) {
        chunk_entry = chunks_.front();
        chunks_.pop_front();
      } else {
        log("send loop stopped");
        DCHECK(stop_);
        return;
      }
    }

    bool ok = false;
    if (is_info) {
      ok = Transfer::SendInfo(channel_.get(), info_entry.info, info_entry.reqs);
    } else {
      const DataHeader& chunk = chunk_entry.chunk;
      DCHECK(chunk.IsData());
      const auto payload = chunk_entry.GenPayload();
      ok = Transfer::SendChunk(channel_.get(), chunk, payload);
      if (ok) {
        metrics_.write.bytes.Add(payload.size());
        if (channel_->Type().IsOneSidedChannel()) {
          // For one-sided RDMA channels, successful SendChunk completes the
          // transfer at the hardware level (confirmed via CQE), with no
          // software ACK chunk sent by the receiver.
          static_assert(
              assumptions::kOneSidedRdmaCompletionIsTrackedBySenderHardwareCqe);
          outgoing_.Update(chunk.handle, chunk.reqid, chunk.index);
        }
      }
    }
    if (!ok) {
      if (channel_->Type().IsMessageChannel()) {
        absl::MutexLock _(mu_);
        if (!stop_) continue;
        LOG(INFO) << "send loop stopped";
        return;
      }
      // TODO(yongx): Handle errors.
      metrics_.write.errors.Add(1);
      LOG(WARNING) << "send chunk failed";
      break;
    }
  }
}

void Worker::RecvLoop() {
  log("recv loop started");
  if (channel_->Type().IsOneSidedChannel()) {
    // One-sided channels like RDMA do not run software code on recv side.
    static_assert(
        assumptions::kOneSidedRdmaCompletionIsTrackedBySenderHardwareCqe);
    absl::MutexLock _(mu_);
    mu_.Await(absl::Condition(&stop_));
    log("recv loop stopped");
    return;
  }

  while (true) {
    {
      absl::MutexLock _(mu_);
      if (stop_) {
        log("recv loop stopped");
        return;
      }
    }
    if (!Transfer::Recv(channel_.get(), outgoing_, incoming_)) {
      if (channel_->Type().IsMessageChannel()) {
        absl::MutexLock _(mu_);
        if (!stop_) continue;
        LOG(INFO) << "recv loop stopped";
        return;
      }
      // TODO(yongx): Handle errors.
      LOG(WARNING) << "recv chunk failed";
      break;
    }
  }
}

}  // namespace peregrine::internal
