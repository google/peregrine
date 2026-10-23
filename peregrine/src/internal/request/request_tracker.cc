#include "peregrine/src/internal/request/request_tracker.h"

#include <cstdint>
#include <memory>
#include <utility>

#include "absl/base/optimization.h"
#include "absl/log/check.h"
#include "absl/synchronization/mutex.h"
#include "absl/time/clock.h"
#include "absl/time/time.h"
#include "absl/types/span.h"
#include "peregrine/src/api/transport_types.h"
#include "peregrine/src/internal/base/types.h"
#include "peregrine/src/internal/chunk/chunk_tracker.h"
#include "peregrine/src/internal/chunk/header.h"

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

bool RequestTracker::Add(const Handle handle, const absl::Time start_time,
                         OnComplete on_complete) {
  ReqsTracker rt(/*req_map=*/{}, start_time, std::move(on_complete));
  absl::MutexLock _(trackers_mu_);
  return trackers_.try_emplace(handle, std::move(rt)).second;
}

bool RequestTracker::Add(const Handle handle,
                         absl::Span<const RequestInfo> reqs) {
  DCHECK(!reqs.empty());
  absl::MutexLock _(trackers_mu_);
  ReqsTracker& rt = trackers_[handle];
  if (!rt.req_map.empty()) return true;
  rt.req_map.reserve(reqs.size());
  for (const RequestInfo& req : reqs) {
    DCHECK(req.IsValid());
    rt.req_map[req.reqid] = std::make_unique<ChunkTracker>(req.nchunks);
  }
  return true;
}

void RequestTracker::Remove(const Handle handle) {
  absl::MutexLock _(trackers_mu_);
  trackers_.erase(handle);
}

void RequestTracker::Update(const Handle handle, const ReqId reqid,
                            const chunk_t index) {
  OnComplete callback = nullptr;
  absl::Time start_time = ReqsTracker::kInvalidTime;
  {
    absl::MutexLock lock(trackers_mu_);
    auto it = trackers_.find(handle);
    if ABSL_PREDICT_FALSE (it == trackers_.end()) return;

    ReqsTracker& rt = it->second;
    ChunkTracker& tracker = *rt.req_map.at(reqid);
    tracker.Set(index);

    if (tracker.IsDone() && isComplete(rt)) {
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

ChunkTracker& RequestTracker::Find(const Handle handle, const ReqId reqid) {
  absl::MutexLock _(trackers_mu_);
  return *trackers_.at(handle).req_map.at(reqid);
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
