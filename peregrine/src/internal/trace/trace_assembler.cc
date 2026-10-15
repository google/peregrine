#include "peregrine/src/internal/trace/trace_assembler.h"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <utility>
#include <vector>

#include "absl/container/flat_hash_map.h"
#include "absl/types/span.h"
#include "peregrine/src/api/transport_trace.h"
#include "peregrine/src/api/transport_types.h"
#include "peregrine/src/internal/base/types.h"
#include "peregrine/src/internal/trace/trace_event.h"

namespace peregrine::internal {

namespace {

// Timelines being assembled, with requests and chunks keyed by ReqId and index.
struct RequestState {
  RequestTimeline timeline;
  absl::flat_hash_map<uint32_t, ChunkTimeline> chunks;
};

struct PostState {
  PostTimeline timeline;
  absl::flat_hash_map<uint32_t, RequestState> requests;
};

// Returns true if t_us precedes the recorded timestamp, which is 0 if none.
bool IsEarliest(uint64_t t_us, uint64_t recorded_us) {
  return recorded_us == 0 || t_us < recorded_us;
}

// Returns the Op in event metadata, or kWrite if it is invalid (e.g. torn).
Op DecodeOp(uint8_t metadata) {
  return metadata == static_cast<uint8_t>(Op::kRead) ? Op::kRead : Op::kWrite;
}

RequestState* FindOrAddRequest(PostState& post, const TraceEvent& ev) {
  if (ev.reqid == kInvalidReqId.value()) return nullptr;
  return &post.requests[ev.reqid];
}

ChunkTimeline* FindOrAddChunk(PostState& post, const TraceEvent& ev) {
  if (ev.chunk_idx == TraceEvent::kNoChunk) return nullptr;
  RequestState* req = FindOrAddRequest(post, ev);
  return req == nullptr ? nullptr : &req->chunks[ev.chunk_idx];
}

// Records a sender worker stage, each of which carries the chunk size and the
// sender worker ID.
void RecordSendStage(PostState& post, const TraceEvent& ev,
                     uint64_t ChunkTimeline::* t_us) {
  ChunkTimeline* c = FindOrAddChunk(post, ev);
  if (c == nullptr) return;
  c->size_bytes = ev.metric1;
  c->send_worker_id = ev.worker_id;
  c->*t_us = ev.timestamp_us;
}

void ProcessEvent(const TraceEvent& ev, PostState& post) {
  PostTimeline& t = post.timeline;
  switch (ev.stage) {
    case TraceStage::kPostEntry:
      t.t_entry_us = ev.timestamp_us;
      break;

    case TraceStage::kEngineEnqueued:
      t.t_enqueued_us = ev.timestamp_us;
      break;

    // Connection setup runs once per Post(), before any request is dequeued.
    case TraceStage::kConnSetupStart:
      t.t_conn_start_us = ev.timestamp_us;
      break;

    case TraceStage::kGetPeerHostInfoDone:
      t.t_host_info_done_us = ev.timestamp_us;
      break;

    case TraceStage::kConnSetupDone:
      t.t_conn_done_us = ev.timestamp_us;
      t.num_channels = ev.metric1;
      break;

    case TraceStage::kAllChunksDone:
      if (IsEarliest(ev.timestamp_us, t.t_all_done_us)) {
        t.t_all_done_us = ev.timestamp_us;
        t.all_done_worker_id = ev.worker_id;
      }
      break;

    case TraceStage::kCallbackDone:
      t.t_completed_us = ev.timestamp_us;
      t.completion_mode = CompletionMode::kCallback;
      break;

    case TraceStage::kPollReaped:
      t.t_completed_us = ev.timestamp_us;
      t.completion_mode = CompletionMode::kPoll;
      break;

    case TraceStage::kEngineDequeued:
      if (RequestState* req = FindOrAddRequest(post, ev); req != nullptr) {
        req->timeline.op = DecodeOp(ev.metadata);
        req->timeline.len = ev.metric1;
        req->timeline.t_dequeued_us = ev.timestamp_us;
      }
      break;

    case TraceStage::kWorkerEnqueued:
      RecordSendStage(post, ev, &ChunkTimeline::t_enqueued_us);
      break;

    case TraceStage::kWorkerDequeued:
      RecordSendStage(post, ev, &ChunkTimeline::t_dequeued_us);
      break;

    case TraceStage::kTxWriteDone:
      RecordSendStage(post, ev, &ChunkTimeline::t_tx_done_us);
      break;

    case TraceStage::kTxAckRcvd:
      if (ChunkTimeline* c = FindOrAddChunk(post, ev);
          c != nullptr && IsEarliest(ev.timestamp_us, c->t_ack_us)) {
        c->t_ack_us = ev.timestamp_us;
        c->recv_worker_id = ev.worker_id;
        c->rx_payload_us = ev.metric1;
        c->rx_acquire_us = ev.metric2;
        c->rx_chunk_status = ev.metadata;
      }
      break;

    // kRdma* stages are not recorded yet.
    case TraceStage::kRdmaPostSend:
    case TraceStage::kRdmaCqeReaped:
      break;
  }
}

absl::flat_hash_map<uint32_t, PostState> GroupEventsByHandle(
    absl::Span<const TraceEvent> events) {
  absl::flat_hash_map<uint32_t, PostState> posts;
  for (const TraceEvent& ev : events) {
    // Skips cleared ring buffer slots.
    if (ev.handle == kInvalidHandle.value() || ev.timestamp_us == 0) continue;
    ProcessEvent(ev, posts[ev.handle]);
  }
  return posts;
}

// Moves the requests and chunks of post into vectors sorted by T_EngineDequeued
// and chunk index.
PostTimeline Flatten(uint32_t handle, PostState& post) {
  PostTimeline t = std::move(post.timeline);
  t.handle = Handle(handle);
  t.requests.reserve(post.requests.size());
  for (auto& [reqid, req] : post.requests) {
    RequestTimeline& r = t.requests.emplace_back(std::move(req.timeline));
    r.reqid = reqid;
    r.chunks.reserve(req.chunks.size());
    for (auto& [index, c] : req.chunks) {
      c.index = index;
      r.chunks.push_back(c);
    }
    std::sort(r.chunks.begin(), r.chunks.end(),
              [](const ChunkTimeline& a, const ChunkTimeline& b) {
                return a.index < b.index;
              });
  }
  std::sort(t.requests.begin(), t.requests.end(),
            [](const RequestTimeline& a, const RequestTimeline& b) {
              return std::pair(a.t_dequeued_us, a.reqid) <
                     std::pair(b.t_dequeued_us, b.reqid);
            });
  return t;
}

ChunkTraceSummary Summarize(const ChunkTimeline& c) {
  return {
      .chunk_index = c.index,
      .size_bytes = c.size_bytes,
      .send_worker_id = c.send_worker_id,
      .recv_worker_id = c.recv_worker_id,
      .worker_queue_wait_us = Elapsed(c.t_enqueued_us, c.t_dequeued_us),
      .tx_write_us = Elapsed(c.t_dequeued_us, c.t_tx_done_us),
      .rx_acquire_wait_us = c.rx_acquire_us,
      .rx_payload_write_us = c.rx_payload_us,
      .chunk_rtt_us = Elapsed(c.t_dequeued_us, c.t_ack_us),
      .rx_chunk_status = c.rx_chunk_status,
  };
}

RequestTraceSummary Summarize(const RequestTimeline& r) {
  RequestTraceSummary summary = {.reqid = r.reqid, .op = r.op};
  size_t chunk_bytes = 0;
  uint64_t t_last_enqueued_us = 0;
  summary.chunks.reserve(r.chunks.size());
  for (const ChunkTimeline& c : r.chunks) {
    chunk_bytes += c.size_bytes;
    t_last_enqueued_us = std::max(t_last_enqueued_us, c.t_enqueued_us);
    summary.chunks.push_back(Summarize(c));
  }
  summary.len = chunk_bytes > 0 ? chunk_bytes : r.len;
  summary.chunk_fanout_us = Elapsed(r.t_dequeued_us, t_last_enqueued_us);
  return summary;
}

PostTraceRecord Summarize(const PostTimeline& t) {
  PostTraceRecord record = {
      .handle = t.handle,
      .post_start_us = t.t_entry_us,
      .enqueue_overhead_us = Elapsed(t.t_entry_us, t.t_enqueued_us),
      .engine_queue_wait_us = Elapsed(t.t_enqueued_us, EngineStartUs(t)),
      .conn_setup_us = Elapsed(t.t_conn_start_us, t.t_conn_done_us),
      .transport_e2e_us = Elapsed(t.t_entry_us, t.t_all_done_us),
      .completion_pickup_us = Elapsed(t.t_all_done_us, t.t_completed_us),
      .completion_mode = t.completion_mode,
  };
  record.requests.reserve(t.requests.size());
  for (const RequestTimeline& r : t.requests) {
    record.requests.push_back(Summarize(r));
  }
  return record;
}

}  // namespace

uint64_t Elapsed(uint64_t start_us, uint64_t end_us) {
  return start_us > 0 && end_us > start_us ? end_us - start_us : 0;
}

uint64_t EngineStartUs(const PostTimeline& t) {
  if (t.t_conn_start_us > 0) return t.t_conn_start_us;
  return t.requests.empty() ? 0 : t.requests.front().t_dequeued_us;
}

std::vector<PostTimeline> TraceAssembler::AssembleTimelines(
    absl::Span<const TraceEvent> events) {
  absl::flat_hash_map<uint32_t, PostState> posts = GroupEventsByHandle(events);
  std::vector<PostTimeline> timelines;
  timelines.reserve(posts.size());
  for (auto& [handle, post] : posts) {
    // Skips Post()s whose head was overwritten in the ring buffer.
    if (post.timeline.t_entry_us == 0) continue;
    timelines.push_back(Flatten(handle, post));
  }
  std::sort(timelines.begin(), timelines.end(),
            [](const PostTimeline& a, const PostTimeline& b) {
              return std::pair(a.t_entry_us, a.handle.value()) <
                     std::pair(b.t_entry_us, b.handle.value());
            });
  return timelines;
}

std::vector<PostTraceRecord> TraceAssembler::Assemble(
    absl::Span<const TraceEvent> events) {
  const std::vector<PostTimeline> timelines = AssembleTimelines(events);
  std::vector<PostTraceRecord> records;
  records.reserve(timelines.size());
  for (const PostTimeline& t : timelines) {
    records.push_back(Summarize(t));
  }
  return records;
}

}  // namespace peregrine::internal
