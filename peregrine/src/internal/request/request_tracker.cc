#include "peregrine/src/internal/request/request_tracker.h"

#include <cstdint>
#include <memory>
#include <utility>
#include <vector>

#include "absl/base/optimization.h"
#include "absl/log/check.h"
#include "absl/synchronization/mutex.h"
#include "absl/types/span.h"
#include "peregrine/src/api/transport_types.h"
#include "peregrine/src/internal/base/endpoint.h"
#include "peregrine/src/internal/base/types.h"
#include "peregrine/src/internal/chunk/chunk_tracker.h"
#include "peregrine/src/internal/chunk/header.h"
#include "peregrine/src/internal/trace/trace_context.h"
#include "peregrine/src/internal/trace/trace_recorder.h"
#include "peregrine/src/util/clock.h"

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

Status RequestTracker::Check(const Handle handle, const Endpoint& peer) const {
  absl::MutexLock _(trackers_mu_);
  const auto it = trackers_.find({peer, handle});
  if ABSL_PREDICT_FALSE (it == trackers_.end()) {
    return Status::kNotFound;
  }
  if (!isComplete(it->second)) {
    return Status::kInProgress;
  }
  return Status::kSuccess;
}

bool RequestTracker::Add(const Handle handle, const uint64_t start_mono_us,
                         OnComplete on_complete,
                         const TraceContext& trace_ctx) {
  PostTracker pt(/*req_map=*/{}, start_mono_us, std::move(on_complete),
                 trace_ctx);
  absl::MutexLock _(trackers_mu_);
  return trackers_.try_emplace(Key{Endpoint{}, handle}, std::move(pt)).second;
}

bool RequestTracker::Add(const Handle handle,
                         absl::Span<const RequestInfo> reqs,
                         const uint64_t start_us, const Metadata& metadata,
                         const Endpoint& peer) {
  DCHECK(!reqs.empty());
  // True if this is a remote `Op::kRead` request received by the responder.
  bool is_responder_read = false;
  std::vector<RequestInfo> resolved_reqs;
  {
    absl::MutexLock _(trackers_mu_);
    auto [it, inserted] = trackers_.try_emplace(Key{peer, handle});
    PostTracker& pt = it->second;
    if (!pt.req_map.empty()) return true;
    pt.metadata = metadata;
    if (!pt.is_initiator && reqs[0].op == Op::kWrite) {
      pt.start_mono_us = start_us;
    }
    is_responder_read = reqs[0].op == Op::kRead && !pt.is_initiator;
    pt.pending_reqs = is_responder_read ? 1 : reqs.size();
    if (is_responder_read) {
      resolved_reqs.reserve(reqs.size());
    }
    pt.req_map.reserve(reqs.size());
    for (const RequestInfo& req : reqs) {
      DCHECK(req.IsValid());
      addr_t raddr =
          pt.is_initiator
              ? req.raddr
              : (resolve_addr_callback_ != nullptr
                     ? addr_t(reinterpret_cast<uintptr_t>(
                           resolve_addr_callback_(metadata, req.metadata)))
                     : addr_t(0));
      if (!pt.is_initiator && raddr.value() == 0) {
        // TODO: Send a NACK / error header back to the sender when address
        // resolution fails so the sender can complete with `Status::kFailure`.
        if (inserted) {
          trackers_.erase(it);
        } else {
          pt.req_map.clear();
          pt.pending_reqs = 0;
          pt.start_mono_us = 0;
        }
        return false;
      }
      pt.req_map[req.reqid] =
          std::make_unique<ReqTracker>(req.nchunks, raddr, req.metadata);
      if (is_responder_read) {
        RequestInfo resolved = req;
        resolved.raddr = raddr;
        resolved_reqs.push_back(resolved);
      }
    }
  }
  if (is_responder_read && on_recv_read_ != nullptr) {
    on_recv_read_(peer, handle, resolved_reqs);
  }
  return true;
}

void RequestTracker::Remove(const Handle handle, const Endpoint& peer) {
  absl::MutexLock _(trackers_mu_);
  auto it = trackers_.find({peer, handle});
  if (it == trackers_.end()) return;
  tracer_.RecordPollReaped(it->second.trace_ctx, handle);
  trackers_.erase(it);
}

void RequestTracker::OnReqComplete(const Handle handle, const Endpoint& peer) {
  OnComplete callback = nullptr;
  bool is_initiator = false;
  uint64_t recv_start_us = 0;
  Metadata post_meta = {};
  std::vector<Metadata> req_metas;
  {
    absl::MutexLock _(trackers_mu_);
    const auto it = trackers_.find({peer, handle});
    if ABSL_PREDICT_FALSE (it == trackers_.end()) return;

    PostTracker& pt = it->second;
    DCHECK_GT(pt.pending_reqs, 0);
    if (--pt.pending_reqs != 0) return;

    is_initiator = pt.is_initiator;
    if (is_initiator) {
      if (pt.start_mono_us != 0) {
        metrics_.read.e2e_latency_us.Record(util::MonoNowUs() -
                                            pt.start_mono_us);
      }
      if (pt.on_complete != nullptr) {
        callback = std::move(pt.on_complete);
        trackers_.erase(it);
      }
    } else {
      recv_start_us = pt.start_mono_us;
      if (recv_post_callback_ != nullptr) {
        post_meta = pt.metadata;
        req_metas.reserve(pt.req_map.size());
        for (const auto& [reqid, req] : pt.req_map) {
          req_metas.push_back(req->metadata);
        }
        // TODO: Evict `it` once `PostInfoHeader` is only sent on active data
        // channels and `sendAck` is called before `OnReqComplete`.
      }
    }
  }
  if (recv_start_us != 0) {
    metrics_.recv_post_latency_us.Record(util::MonoNowUs() - recv_start_us);
  }
  if (callback != nullptr) {
    std::move(callback)(Status::kSuccess);
  } else if (!is_initiator && recv_post_callback_ != nullptr) {
    recv_post_callback_(Status::kSuccess, post_meta, req_metas);
  }
}

void RequestTracker::Update(const Handle handle, const ReqId reqid,
                            const chunk_t index, const int worker_id,
                            const Endpoint& peer) {
  OnComplete callback = nullptr;
  uint64_t start_mono_us = 0;
  TraceContext trace_ctx;
  {
    absl::MutexLock lock(trackers_mu_);
    auto it = trackers_.find({peer, handle});
    if ABSL_PREDICT_FALSE (it == trackers_.end()) return;

    PostTracker& pt = it->second;
    ChunkTracker& tracker = pt.req_map.at(reqid)->chunks;
    tracker.Set(index);
    if (!tracker.IsDone()) return;

    tracer_.RecordRequestDone(pt.trace_ctx, handle, reqid, index, worker_id);
    if (!isComplete(pt)) return;

    start_mono_us = pt.start_mono_us;
    trace_ctx = pt.trace_ctx;
    tracer_.RecordAllRequestsDone(trace_ctx, handle, reqid, index, worker_id);
    if (pt.on_complete != nullptr) {
      callback = std::move(pt.on_complete);
      trackers_.erase(it);
    }
  }
  if (start_mono_us != 0) {
    metrics_.write.e2e_latency_us.Record(util::MonoNowUs() - start_mono_us);
  }
  if (callback != nullptr) {
    std::move(callback)(Status::kSuccess);
    tracer_.RecordCallbackDone(trace_ctx, handle, worker_id);
  }
}

Byte* RequestTracker::GetChunkAddr(const Handle handle, const ReqId reqid,
                                   const uint64_t byte_offset,
                                   const Endpoint& peer) const {
  absl::MutexLock _(trackers_mu_);
  const auto it = trackers_.find({peer, handle});
  if ABSL_PREDICT_FALSE (it == trackers_.end()) return nullptr;
  const auto req_it = it->second.req_map.find(reqid);
  if ABSL_PREDICT_FALSE (req_it == it->second.req_map.end()) return nullptr;
  const uintptr_t base = req_it->second->raddr.value();
  if ABSL_PREDICT_FALSE (base == 0) return nullptr;
  return reinterpret_cast<Byte*>(base + byte_offset);
}

ChunkTracker& RequestTracker::Find(const Handle handle, const ReqId reqid,
                                   const Endpoint& peer) {
  absl::MutexLock _(trackers_mu_);
  return trackers_.at({peer, handle}).req_map.at(reqid)->chunks;
}

ChunkTracker& RequestTracker::FindOrCreate(const Handle handle,
                                           const ReqId reqid,
                                           const uint32_t num_chunks,
                                           const Endpoint& peer) {
  absl::MutexLock _(trackers_mu_);
  PostTracker& pt = trackers_[{peer, handle}];
  std::unique_ptr<ReqTracker>& req = pt.req_map[reqid];
  if ABSL_PREDICT_FALSE (req == nullptr) {
    req = std::make_unique<ReqTracker>(num_chunks);
    ++pt.pending_reqs;
  }
  return req->chunks;
}

}  // namespace peregrine::internal
