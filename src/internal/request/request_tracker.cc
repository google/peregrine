#include "src/internal/request/request_tracker.h"

#include <cstdint>
#include <memory>
#include <utility>

#include "absl/base/optimization.h"
#include "absl/log/check.h"
#include "absl/synchronization/mutex.h"
#include "absl/time/clock.h"
#include "absl/time/time.h"
#include "absl/types/span.h"
#include "src/api/transport_types.h"
#include "src/internal/base/types.h"
#include "src/internal/chunk/chunk.h"
#include "src/internal/chunk/chunk_tracker.h"

namespace peregrine::internal {

bool RequestTracker::IsEmpty() const {
  absl::MutexLock _(trackers_mu_);
  return trackers_.empty();
}

bool RequestTracker::isComplete(const ReqsTracker& rt) const {
  if (rt.req_map.empty()) return false;
  for (const auto& [reqid, tracker] : rt.req_map) {
    if (tracker == nullptr || !tracker->IsDone()) return false;
  }
  return true;
}

Status RequestTracker::Check(const Handle handle) const {
  absl::MutexLock _(trackers_mu_);
  const auto it = trackers_.find(handle);
  if ABSL_PREDICT_FALSE (it == trackers_.end()) {
    return Status::kNotFound;
  }
  if (!isComplete(it->second)) {
    return Status::kInProgress;
  }
  return Status::kSuccess;
}

bool RequestTracker::Add(const Handle handle, absl::Span<const ReqId> reqids,
                         const absl::Time start_time, OnComplete on_complete) {
  DCHECK(!reqids.empty());

  ReqMap req_map;
  req_map.reserve(reqids.size());
  for (const ReqId reqid : reqids) {
    // ChunkTracker can only be created after its #chunks is known.
    req_map.emplace(reqid, /*chunk_tracker=*/nullptr);
  }

  ReqsTracker rt(std::move(req_map), start_time, std::move(on_complete));
  absl::MutexLock _(trackers_mu_);
  return trackers_.try_emplace(handle, std::move(rt)).second;
}

void RequestTracker::Remove(const Handle handle) {
  absl::MutexLock _(trackers_mu_);
  trackers_.erase(handle);
}

void RequestTracker::Update(const Handle handle, const ReqId reqid,
                            const uint32_t num_chunks, const chunk_t index) {
  OnComplete callback = nullptr;
  absl::Time start_time = ReqsTracker::kInvalidTime;
  {
    absl::MutexLock lock(trackers_mu_);
    auto it = trackers_.find(handle);
    if ABSL_PREDICT_FALSE (it == trackers_.end()) return;

    ReqsTracker& rt = it->second;
    std::unique_ptr<ChunkTracker>& tracker = rt.req_map[reqid];
    if (tracker == nullptr) {
      tracker = std::make_unique<ChunkTracker>(num_chunks);
    }
    tracker->Set(index);

    if (tracker->IsDone() && isComplete(rt)) {
      start_time = rt.start_time;
      if (rt.on_complete != nullptr) {
        callback = std::move(rt.on_complete);
        trackers_.erase(it);
      }
    }
  }
  if (start_time != ReqsTracker::kInvalidTime) {
    const int64_t us = absl::ToInt64Microseconds(absl::Now() - start_time);
    if (us >= 0) metrics_.write.e2e_latency_us.Record(us);
  }
  if (callback != nullptr) {
    std::move(callback)(Status::kSuccess);
  }
}

ChunkTracker& RequestTracker::FindOrCreate(const Handle handle,
                                           const ReqId reqid,
                                           const uint32_t num_chunks) {
  absl::MutexLock _(trackers_mu_);
  ReqsTracker& rt = trackers_[handle];
  std::unique_ptr<ChunkTracker>& tracker = rt.req_map[reqid];
  if ABSL_PREDICT_FALSE (tracker == nullptr) {
    tracker = std::make_unique<ChunkTracker>(num_chunks);
  }
  return *tracker;
}

}  // namespace peregrine::internal
