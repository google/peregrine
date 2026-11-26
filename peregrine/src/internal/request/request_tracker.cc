#include "peregrine/src/internal/request/request_tracker.h"

#include <cstdint>
#include <limits>
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
#include "peregrine/src/internal/lib/metric_counter.h"
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
  if (it->second.failed) {
    return Status::kFailure;
  }
  if (!isComplete(it->second)) {
    return Status::kInProgress;
  }
  return Status::kSuccess;
}

bool RequestTracker::Add(const Handle handle, const uint64_t start_mono_us,
                         OnComplete&& on_complete,
                         const TraceContext& trace_ctx) {
  absl::MutexLock _(trackers_mu_);
  return trackers_
      .try_emplace(Key{Endpoint{}, handle}, ReqMap{}, start_mono_us,
                   std::move(on_complete), trace_ctx)
      .second;
}

bool RequestTracker::AddInitiatorReqs(const Handle handle,
                                      absl::Span<const RequestInfo> reqs,
                                      const Endpoint& peer) {
  DCHECK(!reqs.empty());
  if ABSL_PREDICT_FALSE (reqs.empty()) return false;
  absl::MutexLock _(trackers_mu_);
  const auto it = trackers_.find({Endpoint{}, handle});
  if ABSL_PREDICT_FALSE (it == trackers_.end() || !it->second.is_initiator) {
    return false;
  }
  PostTracker& pt = it->second;
  if (!pt.req_map.empty()) return true;
  pt.peer = peer;
  pt.pending_reqs = reqs.size();
  pt.req_map.reserve(reqs.size());
  for (const RequestInfo& req : reqs) {
    DCHECK(req.IsValid());
    if ABSL_PREDICT_FALSE (!req.IsValid()) {
      pt.req_map.clear();
      pt.pending_reqs = 0;
      pt.peer = {};
      return false;
    }
    auto [req_it, req_inserted] = pt.req_map.try_emplace(
        req.reqid, std::make_shared<ReqTracker>(req.nchunks, req.raddr,
                                                req.total_len, req.metadata));
    if ABSL_PREDICT_FALSE (!req_inserted) {
      pt.req_map.clear();
      pt.pending_reqs = 0;
      pt.peer = {};
      return false;
    }
  }
  return true;
}

bool RequestTracker::Add(const Handle handle,
                         absl::Span<const RequestInfo> reqs,
                         const uint64_t start_us, const Metadata& metadata,
                         const Endpoint& peer) {
  DCHECK(!reqs.empty());
  if ABSL_PREDICT_FALSE (reqs.empty()) return false;
  for (const RequestInfo& req : reqs) {
    DCHECK(req.IsValid());
    if ABSL_PREDICT_FALSE (!req.IsValid() || req.op != reqs[0].op) {
      return false;
    }
  }
  // True if this is a remote `Op::kRead` request received by the responder.
  bool is_responder_read = false;
  bool resolution_failed = false;
  std::vector<RequestInfo> resolved_reqs;
  {
    absl::MutexLock _(trackers_mu_);
    auto [it, inserted] = trackers_.try_emplace(Key{peer, handle});
    PostTracker& pt = it->second;
    if (pt.failed) return false;
    if (!pt.req_map.empty()) {
      // Idempotent for duplicate `PostInfoHeader` packets on multi-channel
      // posts.
      return true;
    }
    if ABSL_PREDICT_FALSE (pt.is_initiator) {
      return false;
    }
    if ABSL_PREDICT_FALSE (resolve_addr_callback_ == nullptr) {
      pt.failed = true;
      resolution_failed = true;
    } else {
      // Pre-validate `reqid` uniqueness before invoking
      // `resolve_addr_callback_` so we never return false without notifying
      // `recv_post_callback_` after a callback has acquired per-post state.
      pt.req_map.reserve(reqs.size());
      for (const RequestInfo& req : reqs) {
        if ABSL_PREDICT_FALSE (
                !pt.req_map.try_emplace(req.reqid, nullptr).second) {
          trackers_.erase(it);
          return false;
        }
      }
      pt.metadata = metadata;
      if (reqs[0].op == Op::kWrite) {
        pt.start_mono_us = start_us;
      }
      is_responder_read = reqs[0].op == Op::kRead;
      pt.is_responder_read = is_responder_read;
      pt.pending_reqs = is_responder_read ? 1 : reqs.size();
      if (is_responder_read) {
        resolved_reqs.reserve(reqs.size());
      }
      for (const RequestInfo& req : reqs) {
        const absl::Span<Byte> buf =
            resolve_addr_callback_(metadata, req.metadata);
        const uint64_t extent = req.recipe.BufferExtent(req.total_len);
        const addr_t raddr(reinterpret_cast<uintptr_t>(buf.data()));
        if ABSL_PREDICT_FALSE (raddr.value() == 0 || extent == 0 ||
                               extent > buf.size() ||
                               raddr.value() >
                                   std::numeric_limits<uintptr_t>::max() -
                                       buf.size()) {
          pt.req_map.clear();
          pt.pending_reqs = 0;
          pt.start_mono_us = 0;
          pt.failed = true;
          resolution_failed = true;
          break;
        }
        pt.req_map[req.reqid] = std::make_shared<ReqTracker>(
            req.nchunks, raddr, req.total_len, req.metadata);
        if (is_responder_read) {
          RequestInfo resolved = req;
          resolved.raddr = raddr;
          resolved_reqs.push_back(resolved);
        }
      }
    }
  }
  if (resolution_failed) {
    if (recv_post_callback_ != nullptr) {
      std::vector<Metadata> req_metas;
      req_metas.reserve(reqs.size());
      for (const auto& r : reqs) {
        req_metas.push_back(r.metadata);
      }
      recv_post_callback_(Status::kFailure, metadata, req_metas);
    }
    return false;
  }
  if (is_responder_read && on_recv_read_ != nullptr) {
    on_recv_read_(peer, handle, resolved_reqs);
  }
  return true;
}

void RequestTracker::Remove(const Handle handle, const Endpoint& peer) {
  absl::MutexLock _(trackers_mu_);
  const auto it = trackers_.find({peer, handle});
  if (it == trackers_.end()) return;
  tracer_.RecordPollReaped(it->second.trace_ctx, handle);
  trackers_.erase(it);
}

bool RequestTracker::Fail(const Handle handle, const Status status,
                          const Endpoint& peer,
                          MetricCounter<uint64_t>* const error_counter) {
  OnComplete callback = nullptr;
  {
    absl::MutexLock _(trackers_mu_);
    auto it = trackers_.find({peer, handle});
    if (it == trackers_.end()) return false;

    PostTracker& pt = it->second;
    if (pt.on_complete != nullptr) {
      callback = std::move(pt.on_complete);
      trackers_.erase(it);
    } else {
      if (pt.failed) return false;
      pt.failed = true;
    }
    if (error_counter != nullptr) {
      error_counter->Add(1);
    }
  }
  if (callback != nullptr) {
    std::move(callback)(status);
  }
  return true;
}

bool RequestTracker::OnReqComplete(const Handle handle, const Endpoint& peer) {
  OnComplete callback = nullptr;
  bool is_initiator = false;
  uint64_t recv_start_us = 0;
  Metadata post_meta = {};
  std::vector<Metadata> req_metas;
  {
    absl::MutexLock _(trackers_mu_);
    auto it = trackers_.find({peer, handle});
    if (it == trackers_.end() && peer.HasNonzeroIpPort()) {
      auto init_it = trackers_.find({Endpoint{}, handle});
      if (init_it != trackers_.end() && init_it->second.is_initiator &&
          (!init_it->second.peer.HasNonzeroIpPort() ||
           init_it->second.peer == peer)) {
        it = init_it;
      }
    }
    if ABSL_PREDICT_FALSE (it == trackers_.end()) return false;

    PostTracker& pt = it->second;
    DCHECK_GT(pt.pending_reqs, 0);
    if (--pt.pending_reqs != 0) return false;

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
  return true;
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
    const auto req_it = pt.req_map.find(reqid);
    if ABSL_PREDICT_FALSE (req_it == pt.req_map.end() ||
                           req_it->second == nullptr) {
      return;
    }
    ChunkTracker& tracker = req_it->second->chunks;
    if ABSL_PREDICT_FALSE (!tracker.IsValidChunk(index)) return;
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

RequestTracker::ChunkTarget RequestTracker::FindChunk(
    const Handle handle, const ReqId reqid, const uint64_t byte_offset,
    const uint64_t chunk_len, const Endpoint& peer) {
  absl::MutexLock _(trackers_mu_);
  const auto it = trackers_.find({peer, handle});
  if ABSL_PREDICT_FALSE (it == trackers_.end() ||
                         it->second.is_responder_read) {
    return {};
  }
  const auto req_it = it->second.req_map.find(reqid);
  if ABSL_PREDICT_FALSE (req_it == it->second.req_map.end() ||
                         req_it->second == nullptr) {
    return {};
  }
  ReqTracker& req = *req_it->second;
  const uintptr_t base = req.raddr.value();
  if ABSL_PREDICT_FALSE (base == 0) return {};
  if (chunk_len > 0 &&
      (byte_offset > req.total_len ||
       chunk_len > req.total_len - byte_offset)) {
    return {};
  }
  return {.dst = reinterpret_cast<Byte*>(base + byte_offset),
          .tracker = &req.chunks};
}

Byte* RequestTracker::GetChunkAddr(
    const Handle handle, const ReqId reqid, const uint64_t byte_offset,
    const uint64_t chunk_len, const Endpoint& peer,
    std::shared_ptr<ChunkTracker>* const out_tracker) {
  if (out_tracker != nullptr) *out_tracker = nullptr;
  if ABSL_PREDICT_FALSE (chunk_len == 0) return nullptr;
  absl::MutexLock _(trackers_mu_);
  auto it = trackers_.find({peer, handle});
  if (it == trackers_.end() && peer.HasNonzeroIpPort()) {
    // Fallback for locally initiated `Op::kRead` responses arriving on a
    // peer-identified channel. Only match initiator entries whose expected
    // peer matches (or was unspecified).
    auto init_it = trackers_.find({Endpoint{}, handle});
    if (init_it != trackers_.end() && init_it->second.is_initiator &&
        (!init_it->second.peer.HasNonzeroIpPort() ||
         init_it->second.peer == peer)) {
      it = init_it;
    }
  }
  if ABSL_PREDICT_FALSE (it == trackers_.end() ||
                         it->second.is_responder_read) {
    return nullptr;
  }
  auto req_it = it->second.req_map.find(reqid);
  if ABSL_PREDICT_FALSE (req_it == it->second.req_map.end() ||
                         req_it->second == nullptr) {
    return nullptr;
  }
  ReqTracker& req = *req_it->second;
  const uintptr_t base = req.raddr.value();
  if ABSL_PREDICT_FALSE (base == 0) return nullptr;
  // Assumes contiguous placement (`BufferExtent(total_len) == total_len`);
  // validate against the recipe's buffer extent if non-contiguous recipes are
  // enabled.
  if ABSL_PREDICT_FALSE (byte_offset > req.total_len ||
                         chunk_len > req.total_len - byte_offset) {
    return nullptr;
  }
  if ABSL_PREDICT_FALSE (base >
                         std::numeric_limits<uintptr_t>::max() - byte_offset) {
    return nullptr;
  }
  const uintptr_t addr = base + byte_offset;
  if ABSL_PREDICT_FALSE (addr >
                         std::numeric_limits<uintptr_t>::max() - chunk_len) {
    return nullptr;
  }
  if (out_tracker != nullptr) {
    *out_tracker = std::shared_ptr<ChunkTracker>(req_it->second, &req.chunks);
  }
  return reinterpret_cast<Byte*>(addr);
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
  std::shared_ptr<ReqTracker>& req = pt.req_map[reqid];
  if ABSL_PREDICT_FALSE (req == nullptr) {
    req = std::make_shared<ReqTracker>(num_chunks);
    ++pt.pending_reqs;
  }
  return req->chunks;
}

}  // namespace peregrine::internal
