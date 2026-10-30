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
#include "peregrine/src/internal/trace/trace_context.h"
#include "peregrine/src/internal/trace/trace_recorder.h"

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
                         OnComplete on_complete,
                         const TraceContext& trace_ctx) {
  PostTracker pt(/*req_map=*/{}, start_time, std::move(on_complete), trace_ctx);
  absl::MutexLock _(trackers_mu_);
  return trackers_.try_emplace(handle, std::move(pt)).second;
}

bool RequestTracker::Add(const Handle handle,
                         absl::Span<const RequestInfo> reqs,
                         const Metadata& metadata) {
  DCHECK(!reqs.empty());
  absl::MutexLock _(trackers_mu_);
  auto [it, inserted] = trackers_.try_emplace(handle);
  PostTracker& pt = it->second;
  if (!pt.req_map.empty()) return true;
  pt.metadata = metadata;
  pt.pending_reqs = reqs.size();
  pt.req_map.reserve(reqs.size());
  for (const RequestInfo& req : reqs) {
    DCHECK(req.IsValid());
    addr_t raddr = req.raddr;
    if (raddr.value() == 0 && resolve_addr_callback_ != nullptr) {
      raddr = addr_t(reinterpret_cast<uintptr_t>(
          resolve_addr_callback_(metadata, req.metadata)));
    }
    if (pt.start_time == PostTracker::kInvalidTime && raddr.value() == 0) {
      // TODO: Send a NACK / error header back to the sender when address
      // resolution fails so the sender can complete with `Status::kFailure`.
      if (inserted) {
        trackers_.erase(it);
      } else {
        pt.req_map.clear();
        pt.pending_reqs = 0;
      }
      return false;
    }
    pt.req_map[req.reqid] =
        std::make_unique<ReqTracker>(req.nchunks, raddr, req.metadata);
  }
  return true;
}

void RequestTracker::Remove(const Handle handle) {
  absl::MutexLock _(trackers_mu_);
  auto it = trackers_.find(handle);
  if (it == trackers_.end()) return;
  tracer_.RecordPollReaped(it->second.trace_ctx, handle);
  trackers_.erase(it);
}

void RequestTracker::OnReqComplete(const Handle handle) {
  if (recv_post_callback_ == nullptr) return;

  Metadata post_meta = {};
  std::vector<Metadata> req_metas;
  {
    absl::MutexLock _(trackers_mu_);
    const auto it = trackers_.find(handle);
    if ABSL_PREDICT_FALSE (it == trackers_.end()) return;

    PostTracker& pt = it->second;
    DCHECK_GT(pt.pending_reqs, 0);
    if (--pt.pending_reqs != 0) return;

    post_meta = pt.metadata;
    req_metas.reserve(pt.req_map.size());
    for (const auto& [reqid, req] : pt.req_map) {
      req_metas.push_back(req->metadata);
    }
    // TODO: Evict `it` once `PostInfoHeader` is only sent on active data
    // channels and `sendAck` is called before `OnReqComplete`.
  }
  recv_post_callback_(Status::kSuccess, post_meta, req_metas);
}

void RequestTracker::Update(const Handle handle, const ReqId reqid,
                            const chunk_t index, const int worker_id) {
  OnComplete callback = nullptr;
  absl::Time start_time = PostTracker::kInvalidTime;
  TraceContext trace_ctx;
  {
    absl::MutexLock lock(trackers_mu_);
    auto it = trackers_.find(handle);
    if ABSL_PREDICT_FALSE (it == trackers_.end()) return;

    PostTracker& pt = it->second;
    ChunkTracker& tracker = pt.req_map.at(reqid)->chunks;
    tracker.Set(index);
    if (!tracker.IsDone()) return;

    tracer_.RecordRequestDone(pt.trace_ctx, handle, reqid, index, worker_id);
    if (!isComplete(pt)) return;

    start_time = pt.start_time;
    trace_ctx = pt.trace_ctx;
    tracer_.RecordAllRequestsDone(trace_ctx, handle, reqid, index, worker_id);
    if (pt.on_complete != nullptr) {
      callback = std::move(pt.on_complete);
      trackers_.erase(it);
    }
  }
  if (start_time != PostTracker::kInvalidTime) {
    const int64_t us = absl::ToInt64Microseconds(absl::Now() - start_time);
    if (us >= 0) metrics_.write.e2e_latency_us.Record(us);
  }
  if (callback != nullptr) {
    std::move(callback)(Status::kSuccess);
    tracer_.RecordCallbackDone(trace_ctx, handle, worker_id);
  }
}

Byte* RequestTracker::GetChunkAddr(const Handle handle, const ReqId reqid,
                                   const uint64_t byte_offset) const {
  absl::MutexLock _(trackers_mu_);
  const auto it = trackers_.find(handle);
  if ABSL_PREDICT_FALSE (it == trackers_.end()) return nullptr;
  const auto req_it = it->second.req_map.find(reqid);
  if ABSL_PREDICT_FALSE (req_it == it->second.req_map.end()) return nullptr;
  const uintptr_t base = req_it->second->raddr.value();
  if ABSL_PREDICT_FALSE (base == 0) return nullptr;
  return reinterpret_cast<Byte*>(base + byte_offset);
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
