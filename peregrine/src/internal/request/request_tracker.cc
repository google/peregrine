#include "peregrine/src/internal/request/request_tracker.h"

#include <cstdint>
#include <memory>
#include <utility>
#include <vector>

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

bool RequestTracker::isComplete(const PostTracker& pt) const {
  if (pt.req_map.empty()) return false;
  for (const auto& [reqid, req] : pt.req_map) {
    if (req == nullptr || !req->chunks.IsDone()) return false;
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
  PostTracker pt(/*req_map=*/{}, start_time, std::move(on_complete));
  absl::MutexLock _(trackers_mu_);
  return trackers_.try_emplace(handle, std::move(pt)).second;
}

bool RequestTracker::Add(const Handle handle,
                         absl::Span<const RequestInfo> reqs,
                         const Metadata& metadata) {
  DCHECK(!reqs.empty());
  absl::MutexLock _(trackers_mu_);
  PostTracker& pt = trackers_[handle];
  if (!pt.req_map.empty()) return true;
  pt.metadata = metadata;
  pt.pending_reqs = reqs.size();
  pt.req_map.reserve(reqs.size());
  for (const RequestInfo& req : reqs) {
    DCHECK(req.IsValid());
    pt.req_map[req.reqid] =
        std::make_unique<ReqTracker>(req.nchunks, req.metadata);
  }
  return true;
}

void RequestTracker::Remove(const Handle handle) {
  absl::MutexLock _(trackers_mu_);
  trackers_.erase(handle);
}

void RequestTracker::OnReqComplete(const Handle handle, const ReqId reqid) {
  const bool has_req_cb = recv_req_callback_ != nullptr;
  const bool has_post_cb = recv_post_callback_ != nullptr;
  if (!has_req_cb && !has_post_cb) return;

  Metadata req_meta = {};
  bool post_done = false;
  {
    absl::MutexLock _(trackers_mu_);
    PostTracker& pt = trackers_.at(handle);
    DCHECK_GT(pt.pending_reqs, 0);
    post_done = (--pt.pending_reqs == 0);
    if (has_req_cb) {
      req_meta = pt.req_map.at(reqid)->metadata;
    }
  }
  if (has_req_cb) {
    recv_req_callback_(Status::kSuccess, req_meta);
  }
  if (post_done && has_post_cb) {
    Metadata post_meta = {};
    std::vector<Metadata> req_metas;
    {
      absl::MutexLock _(trackers_mu_);
      const PostTracker& pt = trackers_.at(handle);
      post_meta = pt.metadata;
      req_metas.reserve(pt.req_map.size());
      for (const auto& [id, req] : pt.req_map) {
        req_metas.push_back(req->metadata);
      }
    }
    recv_post_callback_(Status::kSuccess, post_meta, req_metas);
  }
}

void RequestTracker::Update(const Handle handle, const ReqId reqid,
                            const chunk_t index) {
  OnComplete callback = nullptr;
  absl::Time start_time = PostTracker::kInvalidTime;
  {
    absl::MutexLock lock(trackers_mu_);
    auto it = trackers_.find(handle);
    if ABSL_PREDICT_FALSE (it == trackers_.end()) return;

    PostTracker& pt = it->second;
    ChunkTracker& tracker = pt.req_map.at(reqid)->chunks;
    tracker.Set(index);

    if (tracker.IsDone() && isComplete(pt)) {
      start_time = pt.start_time;
      if (pt.on_complete != nullptr) {
        callback = std::move(pt.on_complete);
        trackers_.erase(it);
      }
    }
  }
  if (start_time != PostTracker::kInvalidTime) {
    const int64_t us = absl::ToInt64Microseconds(absl::Now() - start_time);
    if (us >= 0) metrics_.write.e2e_latency_us.Record(us);
  }
  if (callback != nullptr) {
    std::move(callback)(Status::kSuccess);
  }
}

ChunkTracker& RequestTracker::Find(const Handle handle, const ReqId reqid) {
  absl::MutexLock _(trackers_mu_);
  return trackers_.at(handle).req_map.at(reqid)->chunks;
}

ChunkTracker& RequestTracker::FindOrCreate(const Handle handle,
                                           const ReqId reqid,
                                           const uint32_t num_chunks) {
  absl::MutexLock _(trackers_mu_);
  PostTracker& pt = trackers_[handle];
  std::unique_ptr<ReqTracker>& req = pt.req_map[reqid];
  if ABSL_PREDICT_FALSE (req == nullptr) {
    req = std::make_unique<ReqTracker>(num_chunks);
    ++pt.pending_reqs;
  }
  return req->chunks;
}

}  // namespace peregrine::internal
