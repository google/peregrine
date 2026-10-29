#include "peregrine/src/internal/trace/chrome_trace_exporter.h"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "absl/container/flat_hash_map.h"
#include "absl/container/flat_hash_set.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/str_format.h"
#include "absl/strings/str_join.h"
#include "absl/types/span.h"
#include "peregrine/src/api/transport_types.h"
#include "peregrine/src/internal/base/types.h"
#include "peregrine/src/internal/trace/trace_event.h"

namespace peregrine::internal {

namespace {

constexpr int kSenderPid = 1;
constexpr int kReceiverPid = 2;
constexpr int kCallerTid = 1;
constexpr int kEngineTid = 2;
constexpr int kSendWorkerTidBase = 100;
constexpr int kRecvWorkerTidBase = 200;
constexpr int kAckWorkerTidBase = 300;

// How the application picked up a completed Post().
enum class CompletionMode : uint8_t {
  kUnknown = 0,   // Not completed yet, or the completion was not captured
  kCallback = 1,  // on_complete returned (T_CallbackDone)
  kPoll = 2,      // Transport::Poll() reaped the handle (T_PollReaped)
};

// Stage timestamps of a traced chunk, taken from TraceRecorder::NowUs(),
// or 0 if a stage was not captured.
struct ChunkTimeline final {
  uint32_t index = 0;
  uint32_t size_bytes = 0;
  int16_t send_worker_id = 0;
  int16_t recv_worker_id = 0;
  uint64_t t_enqueued_us = 0;  // T_WorkerEnqueued
  uint64_t t_dequeued_us = 0;  // T_WorkerDequeued
  uint64_t t_tx_done_us = 0;   // T_TxWriteDone
  uint64_t t_ack_us = 0;       // T_TxAckRcvd
  // Receiver timings and ChunkStatus piggybacked on the ACK.
  uint32_t rx_acquire_us = 0;
  uint32_t rx_payload_us = 0;
  uint8_t rx_chunk_status = 0;
};

// Stage timestamps of a request within a traced Post().
struct RequestTimeline final {
  uint32_t reqid = 0;
  Op op = Op::kWrite;
  size_t len = 0;
  uint64_t t_dequeued_us = 0;         // T_EngineDequeued
  uint64_t t_done_us = 0;             // T_RequestDone
  int16_t done_worker_id = 0;         // Worker that recorded T_RequestDone
  std::vector<ChunkTimeline> chunks;  // Sorted by index.
};

// Stage timestamps of a traced Post().
struct PostTimeline final {
  Handle handle;
  uint64_t t_entry_us = 0;           // T_PostEntry
  uint64_t t_enqueued_us = 0;        // T_EngineEnqueued
  uint64_t t_conn_start_us = 0;      // T_ConnSetupStart, on cold start only
  uint64_t t_host_info_done_us = 0;  // T_GetPeerHostInfoDone, over TCP only
  uint64_t t_conn_done_us = 0;       // T_ConnSetupDone, on cold start only
  uint32_t num_channels = 0;         // Channels connected by T_ConnSetupDone
  uint64_t t_all_done_us = 0;        // T_AllRequestsDone
  int16_t all_done_worker_id = 0;    // Worker that recorded T_AllRequestsDone
  uint64_t t_completed_us = 0;       // T_CallbackDone or T_PollReaped
  CompletionMode completion_mode = CompletionMode::kUnknown;
  std::vector<RequestTimeline> requests;  // Sorted by T_EngineDequeued.
};

// Timelines being assembled, with requests and chunks keyed by ReqId and index.
struct RequestState {
  RequestTimeline timeline;
  absl::flat_hash_map<uint32_t, ChunkTimeline> chunks;
};

struct PostState {
  PostTimeline timeline;
  absl::flat_hash_map<uint32_t, RequestState> requests;
};

// Returns end_us - start_us, or 0 if either stage was not captured or they are
// out of order.
uint64_t Elapsed(uint64_t start_us, uint64_t end_us) {
  return start_us > 0 && end_us > start_us ? end_us - start_us : 0;
}

// Clamps a slice duration to at least 1 us so sub-microsecond stages remain
// visible in Perfetto.
uint64_t ClampSliceDur(uint64_t dur_us) {
  return std::max<uint64_t>(1, dur_us);
}

uint64_t ClampedElapsed(uint64_t start_us, uint64_t end_us) {
  return ClampSliceDur(Elapsed(start_us, end_us));
}

// Returns when Engine::main_thread_ started processing the Post():
// T_ConnSetupStart on cold start, or the first T_EngineDequeued otherwise.
uint64_t EngineStartUs(const PostTimeline& t) {
  if (t.t_conn_start_us > 0) return t.t_conn_start_us;
  return t.requests.empty() ? 0 : t.requests.front().t_dequeued_us;
}

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

    case TraceStage::kAllRequestsDone:
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

    case TraceStage::kRequestDone:
      if (RequestState* req = FindOrAddRequest(post, ev);
          req != nullptr &&
          IsEarliest(ev.timestamp_us, req->timeline.t_done_us)) {
        req->timeline.t_done_us = ev.timestamp_us;
        req->timeline.done_worker_id = ev.worker_id;
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
    size_t chunk_bytes = 0;
    r.chunks.reserve(req.chunks.size());
    for (auto& [index, c] : req.chunks) {
      c.index = index;
      chunk_bytes += c.size_bytes;
      r.chunks.push_back(c);
    }
    if (chunk_bytes > 0) r.len = chunk_bytes;
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

// Returns a PostTimeline for each Post() whose T_PostEntry is in events,
// sorted by T_PostEntry. Keeps the earliest T_TxAckRcvd, T_RequestDone, and
// T_AllRequestsDone, which repeat if a duplicate chunk is ACKed again.
std::vector<PostTimeline> AssembleTimelines(
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

std::string EscapeJson(std::string_view s) {
  std::string out;
  out.reserve(s.size());
  for (const char c : s) {
    switch (c) {
      case '"':
        out.append("\\\"");
        break;
      case '\\':
        out.append("\\\\");
        break;
      case '\n':
        out.append("\\n");
        break;
      case '\r':
        out.append("\\r");
        break;
      case '\t':
        out.append("\\t");
        break;
      default:
        out.push_back(c);
        break;
    }
  }
  return out;
}

std::string RxChunkStatusName(uint8_t status) {
  switch (status) {
    case 0:
      return "kEmpty";
    case 1:
      return "kBusy";
    case 2:
      return "kDone";
    default:
      return absl::StrFormat("Unknown(%u)", status);
  }
}

// Returns the 1-based sender worker index of a chunk, or one based on the chunk
// index if the sender worker is unknown.
int SendWorkerIdx(const ChunkTimeline& c) {
  return c.send_worker_id > 0 ? c.send_worker_id
                              : static_cast<int>(c.index + 1);
}

// Returns the 1-based receiver worker index of a chunk, or its sender worker
// index if the ACK did not carry one.
int RecvWorkerIdx(const ChunkTimeline& c) {
  return c.recv_worker_id < 0 ? -c.recv_worker_id : SendWorkerIdx(c);
}

// Returns the 1-based ACK worker index for a completion worker_id.
int AckWorkerIdx(int16_t worker_id) {
  return std::max(1, std::abs(static_cast<int>(worker_id)));
}

struct Track {
  int pid;
  int tid;
};

constexpr Track kCallerTrack = {kSenderPid, kCallerTid};
constexpr Track kEngineTrack = {kSenderPid, kEngineTid};

// Serializes assembled PostTimeline records into Chrome Trace Event JSON,
// tracking the last slice end per track so clamped 1 us slices never partially
// overlap in Perfetto.
class TimelineRenderer {
 public:
  std::string Render(absl::Span<const PostTimeline> timelines,
                     std::string_view self_endpoint) {
    for (const PostTimeline& post : timelines) {
      EmitPost(post);
    }
    std::vector<std::string> events = BuildMetadataEvents(self_endpoint);
    events.reserve(events.size() + slice_events_.size());
    for (std::string& ev : slice_events_) {
      events.push_back(std::move(ev));
    }
    return absl::StrCat(
        "{\n  \"displayTimeUnit\": \"ms\",\n  \"traceEvents\": [\n    ",
        absl::StrJoin(events, ",\n    "), "\n  ]\n}\n");
  }

 private:
  Track SendTrack(int sw_idx) {
    send_workers_.insert(sw_idx);
    return {kSenderPid, kSendWorkerTidBase + sw_idx};
  }

  Track RecvTrack(int rw_idx) {
    recv_workers_.insert(rw_idx);
    return {kReceiverPid, kRecvWorkerTidBase + rw_idx};
  }

  Track AckTrack(int sw_idx) {
    ack_workers_.insert(sw_idx);
    return {kSenderPid, kAckWorkerTidBase + sw_idx};
  }

  std::vector<std::string> BuildMetadataEvents(
      std::string_view self_endpoint) const {
    std::vector<std::string> out;
    const std::string sender_name =
        self_endpoint.empty()
            ? "Node A (Sender) — Peregrine Transport"
            : absl::StrFormat("Node A (Sender) — Peregrine Transport [%s]",
                              EscapeJson(self_endpoint));
    AppendProcessMetadata(out, kSenderPid, /*sort_index=*/1, sender_name);
    AppendProcessMetadata(
        out, kReceiverPid, /*sort_index=*/2,
        "Node B (Receiver) — Remote Peers (Piggybacked via ACK ChunkHeader)");

    AppendThreadMetadata(
        out, kCallerTrack,
        absl::StrFormat("Caller / Poller Thread (tid=%d)", kCallerTid));
    AppendThreadMetadata(
        out, kEngineTrack,
        absl::StrFormat("Engine::main_thread_ (tid=%d)", kEngineTid));
    for (const int idx : send_workers_) {
      const Track track = {kSenderPid, kSendWorkerTidBase + idx};
      AppendThreadMetadata(
          out, track,
          absl::StrFormat("Worker #%d::send_thread_ (Tx, tid=%d)", idx,
                          track.tid));
    }
    for (const int idx : ack_workers_) {
      const Track track = {kSenderPid, kAckWorkerTidBase + idx};
      AppendThreadMetadata(
          out, track,
          absl::StrFormat("Worker #%d::recv_thread_ (ACK Rx, tid=%d)", idx,
                          track.tid));
    }
    for (const int idx : recv_workers_) {
      const Track track = {kReceiverPid, kRecvWorkerTidBase + idx};
      AppendThreadMetadata(
          out, track,
          absl::StrFormat("Remote Worker #-%d::recv_thread_ (tid=%d)", idx,
                          track.tid));
    }
    return out;
  }

  void EmitPost(const PostTimeline& post) {
    const uint64_t enqueue_flow_ts = EmitPostEnqueue(post);
    EmitEngineQueueWait(post, enqueue_flow_ts);
    EmitConnSetup(post);
    for (const RequestTimeline& req : post.requests) {
      EmitRequest(post, req);
    }
    EmitCompletion(post);
  }

  // Emits TransportImpl::Post on the caller track and returns the timestamp for
  // the outgoing enqueue_to_engine flow arrow.
  uint64_t EmitPostEnqueue(const PostTimeline& post) {
    const uint32_t handle = post.handle.value();
    const uint64_t enqueue_overhead_us =
        Elapsed(post.t_entry_us, post.t_enqueued_us);
    const uint64_t t_entry_us = std::max(post.t_entry_us, caller_end_us_);
    const uint64_t post_dur_us = ClampedElapsed(t_entry_us, post.t_enqueued_us);
    caller_end_us_ = t_entry_us + post_dur_us;

    size_t total_bytes = 0;
    for (const RequestTimeline& req : post.requests) total_bytes += req.len;

    AddSlice(
        kCallerTrack, "api",
        absl::StrFormat("TransportImpl::Post(handle=0x%x, %zu reqs, %zu B)",
                        handle, post.requests.size(), total_bytes),
        t_entry_us, post_dur_us,
        absl::StrFormat(
            R"json("handle":"0x%x","num_requests":%zu,"total_bytes":%zu,"enqueue_overhead_us":%llu)json",
            handle, post.requests.size(), total_bytes, enqueue_overhead_us));
    return t_entry_us + post_dur_us / 2;
  }

  void EmitEngineQueueWait(const PostTimeline& post, uint64_t enqueue_flow_ts) {
    const uint32_t handle = post.handle.value();
    const uint64_t engine_start_us = EngineStartUs(post);
    const uint64_t engine_queue_wait_us =
        Elapsed(post.t_enqueued_us, engine_start_us);
    const uint64_t q_start_us = std::max(caller_end_us_, engine_end_us_);
    uint64_t cur_engine_us = std::max(q_start_us, engine_start_us);
    if (cur_engine_us > q_start_us || engine_queue_wait_us > 0) {
      const uint64_t q_dur_us = ClampSliceDur(cur_engine_us - q_start_us);
      cur_engine_us = q_start_us + q_dur_us;
      AddSlice(kEngineTrack, "queue",
               absl::StrFormat("Engine::QueueWait(handle=0x%x)", handle),
               q_start_us, q_dur_us,
               absl::StrFormat(
                   R"json("handle":"0x%x","engine_queue_wait_us":%llu)json",
                   handle, engine_queue_wait_us));
    }
    AddFlow("enqueue_to_engine", kCallerTrack, enqueue_flow_ts, kEngineTrack,
            cur_engine_us);
    engine_end_us_ = cur_engine_us;
  }

  void EmitConnSetup(const PostTimeline& post) {
    if (post.t_conn_start_us == 0 && post.t_conn_done_us == 0) return;
    const uint32_t handle = post.handle.value();
    const uint64_t conn_setup_us =
        Elapsed(post.t_conn_start_us, post.t_conn_done_us);
    const uint64_t conn_dur_us =
        ClampedElapsed(engine_end_us_, post.t_conn_done_us);
    AddSlice(kEngineTrack, "conn_setup",
             "Engine::createSendWorkers [Cold-Start]", engine_end_us_,
             conn_dur_us,
             absl::StrFormat(R"json("handle":"0x%x","conn_setup_us":%llu)json",
                             handle, conn_setup_us));

    const uint64_t host_info_dur_us =
        Elapsed(post.t_conn_start_us, post.t_host_info_done_us);
    if (host_info_dur_us > 0 && host_info_dur_us <= conn_dur_us) {
      AddSlice(kEngineTrack, "control", "Control::GetPeerHostInfo (gRPC)",
               engine_end_us_, host_info_dur_us,
               absl::StrFormat(R"json("handle":"0x%x")json", handle));
    }
    const uint64_t connect_dur_us =
        Elapsed(std::max(post.t_conn_start_us, post.t_host_info_done_us),
                post.t_conn_done_us);
    if (connect_dur_us > 0 &&
        host_info_dur_us + connect_dur_us <= conn_dur_us) {
      AddSlice(
          kEngineTrack, "socket",
          absl::StrFormat("Channel::Connect (%u channels)", post.num_channels),
          engine_end_us_ + host_info_dur_us, connect_dur_us,
          absl::StrFormat(R"json("num_channels":%u)json", post.num_channels));
    }
    engine_end_us_ += conn_dur_us;
  }

  void EmitRequest(const PostTimeline& post, const RequestTimeline& req) {
    const uint32_t handle = post.handle.value();
    const uint64_t fanout_start_us = EmitRequestFanout(handle, req);
    for (const ChunkTimeline& c : req.chunks) {
      EmitChunk(handle, req.reqid, fanout_start_us, c);
    }
    EmitRequestDone(post, req);
  }

  uint64_t EmitRequestFanout(uint32_t handle, const RequestTimeline& req) {
    const uint64_t fanout_start_us =
        std::max(engine_end_us_, req.t_dequeued_us);
    uint64_t t_last_enqueued_us = 0;
    for (const ChunkTimeline& c : req.chunks) {
      t_last_enqueued_us = std::max(t_last_enqueued_us, c.t_enqueued_us);
    }
    const uint64_t chunk_fanout_us =
        Elapsed(req.t_dequeued_us, t_last_enqueued_us);
    const uint64_t fanout_dur_us =
        ClampedElapsed(fanout_start_us, t_last_enqueued_us);
    AddSlice(
        kEngineTrack, "engine",
        absl::StrFormat("Engine::process%s(reqid=0x%x, %zu B -> %zu chunks)",
                        req.op == Op::kWrite ? "Write" : "Read", req.reqid,
                        req.len, req.chunks.size()),
        fanout_start_us, fanout_dur_us,
        absl::StrFormat(
            R"json("handle":"0x%x","reqid":"0x%x","nchunks":%zu,"len":%zu,"chunk_fanout_us":%llu)json",
            handle, req.reqid, req.chunks.size(), req.len, chunk_fanout_us));
    engine_end_us_ = fanout_start_us + fanout_dur_us;
    return fanout_start_us;
  }

  void EmitChunk(uint32_t handle, uint32_t reqid, uint64_t fanout_start_us,
                 const ChunkTimeline& c) {
    const int sw_idx = SendWorkerIdx(c);
    const Track send_track = SendTrack(sw_idx);
    const uint64_t worker_queue_wait_us =
        Elapsed(c.t_enqueued_us, c.t_dequeued_us);
    const uint64_t tx_write_us = Elapsed(c.t_dequeued_us, c.t_tx_done_us);

    const uint64_t t_w_enqueued_us =
        c.t_enqueued_us > 0 ? std::max(c.t_enqueued_us, fanout_start_us)
                            : fanout_start_us;
    uint64_t& send_end_us = send_end_us_[sw_idx];
    const bool hol_clamped = t_w_enqueued_us < send_end_us;
    const uint64_t visual_q_start_us = std::max(t_w_enqueued_us, send_end_us);
    uint64_t t_w_dequeued_us = std::max(visual_q_start_us, c.t_dequeued_us);
    const uint64_t visual_q_dur_us =
        Elapsed(visual_q_start_us, t_w_dequeued_us);

    if (visual_q_dur_us > 0 || worker_queue_wait_us > 0) {
      const uint64_t q_dur_us = ClampSliceDur(visual_q_dur_us);
      t_w_dequeued_us = visual_q_start_us + q_dur_us;
      AddSlice(
          send_track, "queue",
          absl::StrFormat("WorkerQueue(req=0x%x, chunk=%u)%s", reqid, c.index,
                          hol_clamped ? " [HoL Wait]" : ""),
          visual_q_start_us, q_dur_us,
          absl::StrFormat(
              R"json("handle":"0x%x","reqid":"0x%x","chunk_index":%u,"enqueued_at_us":%llu,"worker_queue_wait_us":%llu)json",
              handle, reqid, c.index, c.t_enqueued_us, worker_queue_wait_us));
    }

    const uint64_t tx_dur_us = ClampSliceDur(tx_write_us);
    const uint64_t t_tx_done_us = t_w_dequeued_us + tx_dur_us;
    send_end_us = t_tx_done_us;

    AddSlice(
        send_track, "transfer_tx",
        absl::StrFormat("SendChunk(req=0x%x, chunk=%u, %u B)", reqid, c.index,
                        c.size_bytes),
        t_w_dequeued_us, tx_dur_us,
        absl::StrFormat(
            R"json("handle":"0x%x","reqid":"0x%x","chunk_index":%u,"size_bytes":%u,"tx_send_mono_us":%llu,"worker_queue_wait_us":%llu,"tx_write_us":%llu)json",
            handle, reqid, c.index, c.size_bytes, c.t_dequeued_us,
            worker_queue_wait_us, tx_write_us));
    AddFlow(absl::StrFormat("fanout_c%u", c.index), kEngineTrack,
            std::min(t_w_enqueued_us, engine_end_us_), send_track,
            t_w_dequeued_us);

    EmitChunkAck(handle, reqid, c, send_track, t_w_dequeued_us, t_tx_done_us);
  }

  void EmitChunkAck(uint32_t handle, uint32_t reqid, const ChunkTimeline& c,
                    Track send_track, uint64_t t_w_dequeued_us,
                    uint64_t t_tx_done_us) {
    if (c.t_ack_us == 0) return;
    const uint64_t chunk_rtt_us = Elapsed(c.t_dequeued_us, c.t_ack_us);
    const uint64_t t_ack_us =
        c.t_ack_us >= t_tx_done_us
            ? c.t_ack_us
            : std::max(t_tx_done_us, t_w_dequeued_us + chunk_rtt_us);

    const auto [recv_track, t_rx_done] =
        EmitRemoteRecv(handle, reqid, c, send_track, t_w_dequeued_us, t_ack_us);

    const int sw_idx = SendWorkerIdx(c);
    const Track ack_track = AckTrack(sw_idx);
    uint64_t& ack_end_us = ack_end_us_[sw_idx];
    const uint64_t ack_start_us =
        std::max(ack_end_us, t_ack_us > 1 ? t_ack_us - 1 : t_ack_us);
    const uint64_t ack_dur_us =
        t_ack_us >= ack_start_us ? t_ack_us - ack_start_us + 1 : 1;
    ack_end_us = ack_start_us + ack_dur_us;

    AddSlice(
        ack_track, "ack_rx",
        absl::StrFormat("RecvAck & Update(req=0x%x, chunk=%u)", reqid, c.index),
        ack_start_us, ack_dur_us,
        absl::StrFormat(
            R"json("handle":"0x%x","reqid":"0x%x","chunk_index":%u,"chunk_rtt_us":%llu,"piggybacked_rx_acquire_us":%llu,"piggybacked_rx_payload_us":%llu)json",
            handle, reqid, c.index, chunk_rtt_us, c.rx_acquire_us,
            c.rx_payload_us));
    AddFlow(absl::StrFormat("wire_ack_c%u", c.index), recv_track,
            t_rx_done > 0 ? t_rx_done - 1 : 0, ack_track, ack_start_us);
  }

  // Infers remote receiver slices from piggybacked ACK timings and returns the
  // receiver track and inferred completion timestamp.
  std::pair<Track, uint64_t> EmitRemoteRecv(uint32_t handle, uint32_t reqid,
                                            const ChunkTimeline& c,
                                            Track send_track,
                                            uint64_t t_w_dequeued_us,
                                            uint64_t t_ack_us) {
    const int rw_idx = RecvWorkerIdx(c);
    const Track recv_track = RecvTrack(rw_idx);
    const uint64_t rx_total_us = c.rx_acquire_us + c.rx_payload_us;
    const uint64_t rtt_us = t_ack_us > t_w_dequeued_us
                                ? t_ack_us - t_w_dequeued_us
                                : ClampSliceDur(rx_total_us);
    const uint64_t transit_us = rtt_us > rx_total_us ? rtt_us - rx_total_us : 0;
    uint64_t& rx_end_us = rx_end_us_[rw_idx];
    const uint64_t t_rx_start =
        std::max(t_w_dequeued_us + transit_us / 2, rx_end_us);
    const uint64_t rx_acq_dur = ClampSliceDur(c.rx_acquire_us);
    const uint64_t rx_pay_dur = ClampSliceDur(c.rx_payload_us);
    const uint64_t rx_dur = rx_acq_dur + rx_pay_dur;
    const uint64_t t_rx_acq = t_rx_start + rx_acq_dur;
    const uint64_t t_rx_done = t_rx_start + rx_dur;
    rx_end_us = t_rx_done;

    const std::string rx_status = RxChunkStatusName(c.rx_chunk_status);
    AddSlice(
        recv_track, "transfer_rx",
        absl::StrFormat("RecvChunk(req=0x%x, chunk=%u, %u B)", reqid, c.index,
                        c.size_bytes),
        t_rx_start, rx_dur,
        absl::StrFormat(
            R"json("handle":"0x%x","reqid":"0x%x","chunk_index":%u,"rx_acquire_wait_us":%llu,"rx_payload_write_us":%llu,"rx_chunk_status":"%s")json",
            handle, reqid, c.index, c.rx_acquire_us, c.rx_payload_us,
            rx_status));
    AddSlice(recv_track, "tracker", "ChunkTracker::Acquire", t_rx_start,
             rx_acq_dur,
             absl::StrFormat(R"json("status":"%s")json", rx_status));
    AddFlow(absl::StrFormat("wire_data_c%u", c.index), send_track,
            t_w_dequeued_us, recv_track, t_rx_start);
    AddSlice(recv_track, "transfer_rx", "Channel::Read(Payload -> DstAddr)",
             t_rx_acq, rx_pay_dur,
             absl::StrFormat(R"json("size_bytes":%u)json", c.size_bytes));
    AddFlow(absl::StrFormat("rx_acquire_to_read_c%u", c.index), recv_track,
            t_rx_start, recv_track, t_rx_acq);
    return {recv_track, t_rx_done};
  }

  void EmitRequestDone(const PostTimeline& post, const RequestTimeline& req) {
    if (req.t_done_us == 0) return;
    const int sw_idx = AckWorkerIdx(req.done_worker_id);
    AddInstant(
        AckTrack(sw_idx), "completion", 't',
        absl::StrFormat("kRequestDone (req=0x%x)", req.reqid),
        std::max(req.t_done_us, ack_end_us_[sw_idx]),
        absl::StrFormat(
            R"json("handle":"0x%x","reqid":"0x%x","request_e2e_us":%llu)json",
            post.handle.value(), req.reqid,
            Elapsed(post.t_entry_us, req.t_done_us)));
  }

  void EmitCompletion(const PostTimeline& post) {
    if (post.t_all_done_us == 0) return;
    const uint32_t handle = post.handle.value();
    const int sw_idx = AckWorkerIdx(post.all_done_worker_id);
    const Track ack_track = AckTrack(sw_idx);
    uint64_t& ack_end_us = ack_end_us_[sw_idx];
    const uint64_t t_all_done_us = std::max(post.t_all_done_us, ack_end_us);
    const uint64_t transport_e2e_us =
        Elapsed(post.t_entry_us, post.t_all_done_us);
    const uint64_t pickup_us = Elapsed(post.t_all_done_us, post.t_completed_us);

    AddInstant(
        ack_track, "completion", 'p',
        absl::StrFormat("kAllRequestsDone (handle=0x%x)", handle),
        t_all_done_us,
        absl::StrFormat(R"json("handle":"0x%x","transport_e2e_us":%llu)json",
                        handle, transport_e2e_us));

    if (post.completion_mode == CompletionMode::kCallback) {
      const uint64_t cb_dur_us =
          ClampedElapsed(t_all_done_us, post.t_completed_us);
      AddSlice(
          ack_track, "callback",
          "on_complete(Status::kSuccess) [kCallbackDone]", t_all_done_us,
          cb_dur_us,
          absl::StrFormat(
              R"json("handle":"0x%x","transport_e2e_us":%llu,"completion_pickup_us":%llu)json",
              handle, transport_e2e_us, pickup_us));
      ack_end_us = t_all_done_us + cb_dur_us;
    } else if (post.completion_mode == CompletionMode::kPoll) {
      const uint64_t wait_start_us = std::max(t_all_done_us, caller_end_us_);
      const uint64_t t_completed_us =
          std::max(wait_start_us, post.t_completed_us);
      if (const uint64_t wait_dur_us = Elapsed(wait_start_us, t_completed_us);
          wait_dur_us > 0) {
        AddSlice(kCallerTrack, "poll_delay",
                 "PollWaitDelay (Ready before Poll)", wait_start_us,
                 wait_dur_us,
                 absl::StrFormat(
                     R"json("handle":"0x%x","completion_pickup_us":%llu)json",
                     handle, pickup_us));
      }
      AddSlice(
          kCallerTrack, "poll",
          absl::StrFormat("TransportImpl::Poll(0x%x) -> kSuccess [kPollReaped]",
                          handle),
          t_completed_us, /*dur=*/1,
          absl::StrFormat(
              R"json("handle":"0x%x","status":"kSuccess","transport_e2e_us":%llu,"completion_pickup_us":%llu)json",
              handle, transport_e2e_us, pickup_us));
      AddFlow("completion_to_poll", ack_track, t_all_done_us, kCallerTrack,
              t_completed_us);
      caller_end_us_ = t_completed_us + 1;
    }
  }

  static void AppendProcessMetadata(std::vector<std::string>& out, int pid,
                                    int sort_index, std::string_view name) {
    out.push_back(absl::StrFormat(
        R"json({"name":"process_name","cat":"__metadata","ph":"M","pid":%d,"tid":0,"ts":0,"args":{"name":"%s"}})json",
        pid, name));
    out.push_back(absl::StrFormat(
        R"json({"name":"process_sort_index","cat":"__metadata","ph":"M","pid":%d,"tid":0,"ts":0,"args":{"sort_index":%d}})json",
        pid, sort_index));
  }

  static void AppendThreadMetadata(std::vector<std::string>& out, Track track,
                                   std::string_view name) {
    out.push_back(absl::StrFormat(
        R"json({"name":"thread_name","cat":"__metadata","ph":"M","pid":%d,"tid":%d,"ts":0,"args":{"name":"%s"}})json",
        track.pid, track.tid, name));
    out.push_back(absl::StrFormat(
        R"json({"name":"thread_sort_index","cat":"__metadata","ph":"M","pid":%d,"tid":%d,"ts":0,"args":{"sort_index":%d}})json",
        track.pid, track.tid, track.tid));
  }

  void AddSlice(Track track, std::string_view cat, std::string_view name,
                uint64_t ts, uint64_t dur, std::string_view args) {
    slice_events_.push_back(absl::StrFormat(
        R"json({"name":"%s","cat":"%s","ph":"X","pid":%d,"tid":%d,"ts":%llu,"dur":%llu,"args":{%s}})json",
        name, cat, track.pid, track.tid, ts, dur, args));
  }

  void AddInstant(Track track, std::string_view cat, char scope,
                  std::string_view name, uint64_t ts, std::string_view args) {
    slice_events_.push_back(absl::StrFormat(
        R"json({"name":"%s","cat":"%s","ph":"i","s":"%c","pid":%d,"tid":%d,"ts":%llu,"args":{%s}})json",
        name, cat, scope, track.pid, track.tid, ts, args));
  }

  // Must be called after emitting both endpoint slices: Perfetto preserves JSON
  // array order on timestamp ties and binds flow endpoints only to slices
  // already open on the track.
  void AddFlow(std::string_view name, Track src, uint64_t src_ts, Track dst,
               uint64_t dst_ts) {
    const uint64_t id = ++next_flow_id_;
    slice_events_.push_back(absl::StrFormat(
        R"json({"name":"%s","cat":"flow","ph":"s","id":%llu,"pid":%d,"tid":%d,"ts":%llu})json",
        name, id, src.pid, src.tid, src_ts));
    slice_events_.push_back(absl::StrFormat(
        R"json({"name":"%s","cat":"flow","ph":"f","bp":"e","id":%llu,"pid":%d,"tid":%d,"ts":%llu})json",
        name, id, dst.pid, dst.tid, dst_ts));
  }

  std::vector<std::string> slice_events_;
  uint64_t next_flow_id_ = 1000;
  uint64_t caller_end_us_ = 0;
  uint64_t engine_end_us_ = 0;
  absl::flat_hash_set<int> send_workers_;
  absl::flat_hash_set<int> recv_workers_;
  absl::flat_hash_set<int> ack_workers_;
  absl::flat_hash_map<int, uint64_t> send_end_us_;
  absl::flat_hash_map<int, uint64_t> rx_end_us_;
  absl::flat_hash_map<int, uint64_t> ack_end_us_;
};

}  // namespace

std::string ChromeTraceExporter::Export(absl::Span<const TraceEvent> events,
                                        std::string_view self_endpoint) {
  return TimelineRenderer().Render(AssembleTimelines(events), self_endpoint);
}

}  // namespace peregrine::internal
