#include "peregrine/src/internal/engine2/tcp_channel_session.h"

#include <sys/types.h>

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <variant>
#include <vector>

#include "absl/base/optimization.h"
#include "absl/log/check.h"
#include "absl/log/log.h"
#include "peregrine/src/api/transport_types.h"
#include "peregrine/src/internal/base/endpoint.h"
#include "peregrine/src/internal/base/types.h"
#include "peregrine/src/internal/channel/channel.h"
#include "peregrine/src/internal/chunk/chunk_tracker.h"
#include "peregrine/src/internal/chunk/header.h"
#include "peregrine/src/internal/chunk/header_flatbuf.h"
#include "peregrine/src/internal/engine2/peer_work_queue.h"
#include "peregrine/src/internal/lib/iovec_cursor.h"
#include "peregrine/src/internal/metrics/engine_metrics.h"
#include "peregrine/src/internal/request/request_tracker.h"
#include "peregrine/src/internal/trace/chunk_trace_wire.h"
#include "peregrine/src/internal/trace/trace_recorder.h"
#include "peregrine/src/util/clock.h"

namespace peregrine::internal::engine2 {
namespace {

using ChunkStatus = ChunkTracker::ChunkStatus;

// Max frames written per `DriveTx()` call before yielding to RX/other channels.
constexpr int kWriteBudget = 16;

// Max header/payload steps read per `DriveRx()` call before yielding (32 steps
// = 16 header+payload frames, matching `kWriteBudget`).
constexpr int kReadBudget = 32;

// Scratch buffer size used when discarding unwanted chunk payloads.
constexpr size_t kDrainBufSize = 64 * 1024;

// Stamps receiver timings into `chunk.trace` before ACK serialization.
void StampRxTrace(DataHeader& chunk, int rx_worker_id, ChunkStatus status,
                  uint64_t t_start_us, uint64_t t_acquired_us) {
  if (ABSL_PREDICT_TRUE(!chunk.trace.IsTraced())) return;
  chunk.trace.SetRx(rx_worker_id, status, t_start_us, t_acquired_us,
                    util::MonoNowUs());
}

}  // namespace

TcpChannelSession::TcpChannelSession(std::unique_ptr<Channel> channel,
                                     PeerId remote_peer,
                                     PeerWorkQueue& work_queue,
                                     RequestTracker& outgoing,
                                     RequestTracker& incoming,
                                     EngineMetrics& metrics,
                                     TraceRecorder& tracer, int worker_id)
    : worker_id_(worker_id),
      remote_peer_(remote_peer),
      work_queue_(work_queue),
      outgoing_(outgoing),
      incoming_(incoming),
      metrics_(metrics),
      tracer_(tracer),
      channel_(std::move(channel)) {
  DCHECK(channel_ != nullptr);
  DCHECK_GE(worker_id_, 0);
}

TcpChannelSession::~TcpChannelSession() { abortInFlight(); }

bool TcpChannelSession::DriveTx(bool& has_more) {
  has_more = false;
  if (tx_.blocked) return true;

  for (int sent = 0; sent < kWriteBudget; ++sent) {
    // Stage the next frame if no partial write is in progress.
    if (!tx_.cursor.has_value() && !prepareNextTx()) {
      return true;
    }
    const ssize_t n = channel_->Write(*tx_.cursor);
    if (ABSL_PREDICT_FALSE(n < 0)) {
      LOG(WARNING) << "worker #" << worker_id_ << " write failed on "
                   << *channel_;
      return false;
    }
    // Partial write / EAGAIN: pause until EPOLLOUT clears backpressure.
    if (tx_.cursor->RemainingBytes() > 0) {
      tx_.blocked = true;
      return true;
    }
    if (tx_.payload_len > 0) {
      metrics_.write.bytes.Add(tx_.payload_len);
    }
    if (ABSL_PREDICT_FALSE(tx_.traced_chunk.has_value())) {
      tracer_.RecordTxWriteDone(*tx_.traced_chunk, traceTxWorkerId());
    }
    tx_.ResetFrame();
  }
  // Hit fairness budget without blocking; caller should re-poll without sleep.
  has_more = true;
  return true;
}

bool TcpChannelSession::DriveRx(bool& has_more, const bool peer_closed) {
  has_more = false;
  for (int steps = 0; steps < kReadBudget; ++steps) {
    if (!rx_.cursor.has_value()) prepareRxCursor();

    const ssize_t n = channel_->Read(*rx_.cursor);
    if (ABSL_PREDICT_FALSE(n < 0)) {
      LOG(WARNING) << "worker #" << worker_id_ << " read failed on "
                   << *channel_;
      return false;
    }
    // Partial read / EAGAIN, or EOF once no more buffered bytes remain.
    if (rx_.cursor->RemainingBytes() > 0) {
      if (peer_closed) {
        if (n == 0) {
          LOG(INFO) << "worker #" << worker_id_ << " peer closed " << *channel_;
          return false;
        }
        // `TcpSocket::Recv` returns `n > 0` when EOF follows partial bytes;
        // re-read so EOF is observed instead of stalling under EPOLLET.
        continue;
      }
      return true;
    }

    // Completed the current header or payload; advance the RX state machine.
    rx_.cursor.reset();
    if (auto* hdr = std::get_if<RxState::ReadingHeader>(&rx_.phase)) {
      if (ABSL_PREDICT_FALSE(!onHeader(*hdr))) return false;
    } else {
      onPayload();
    }
  }
  // Hit fairness budget before EAGAIN; caller must re-drive without sleeping.
  has_more = true;
  return true;
}

void TcpChannelSession::abortInFlight() {
  // Release any acquired incoming chunk slot so it doesn't stay stuck kBusy.
  if (auto* data = std::get_if<RxState::ReadingData>(&rx_.phase)) {
    data->tracker->Release(data->header.index, /*success=*/false);
    rx_.phase.emplace<RxState::ReadingHeader>();
    rx_.cursor.reset();
  }
  // Fail any in-flight outgoing post so callers do not hang in kInProgress.
  if (tx_.post != nullptr) {
    outgoing_.Fail(tx_.post->post.handle, Status::kFailure, {},
                   &metrics_.write.errors);
    tx_.ResetFrame();
  }
}

bool TcpChannelSession::prepareNextTx() {
  // 1. Flush all queued control frames together ahead of bulk data chunks.
  if (const std::optional<IoVec> iov = tx_.control.TakeBatch();
      iov.has_value()) {
    const std::array<IoVec, 1> iovs = {*iov};
    tx_.cursor.emplace(iovs);
    return true;
  }
  // 2. Pull the next chunk from the peer's queue; prepend PostInfo once/post.
  std::optional<ChunkClaim> claim = work_queue_.TryPull();
  if (!claim.has_value()) return false;

  const Handle handle = claim->post->post.handle;
  const bool need_post_info = handle != tx_.last_post_info_handle;
  if (need_post_info) tx_.last_post_info_handle = handle;
  prepareChunkTx(*claim, need_post_info);
  return true;
}

void TcpChannelSession::prepareChunkTx(const ChunkClaim& claim,
                                       const bool include_post_info) {
  const QueuedPost& post = *claim.post;
  const RequestInfo& info = post.req_infos[claim.request_index];
  const Request& req = post.post.requests[claim.request_index];
  const uint64_t offset = uint64_t{claim.chunk_index} * kChunkSize;
  DCHECK_LT(offset, info.total_len);
  const size_t len = std::min<uint64_t>(kChunkSize, info.total_len - offset);

  // Serialize the DataHeader and pin `post` so `req.laddr` stays alive.
  DataHeader chunk = {};
  chunk.opcode = Opcode::kData;
  chunk.handle = post.post.handle;
  chunk.len = static_cast<uint32_t>(len);
  chunk.reqid = info.reqid;
  chunk.index = chunk_t(claim.chunk_index);
  chunk.byte_offset = offset;
  chunk.raddr =
      info.raddr.value() == 0 ? addr_t(0) : addr_t(info.raddr.value() + offset);
  if (ABSL_PREDICT_FALSE(post.post.trace_ctx.is_traced)) {
    chunk.trace.SetTraced();
    tracer_.RecordWorkerDequeued(chunk, traceTxWorkerId());
    tx_.traced_chunk = chunk;
  }
  HeaderUtil::Serialize(chunk, tx_.header);
  tx_.post = claim.post;
  tx_.payload_len = len;

  // Coalesce [PostInfo, DataHeader, payload] into one writev on a new post.
  if (include_post_info) {
    DCHECK(!post.post_info.empty());
    const std::array<IoVec, 3> iovs = {
        IoVec{const_cast<Byte*>(post.post_info.data()), post.post_info.size()},
        IoVec{tx_.header.data(), tx_.header.size()},
        IoVec{const_cast<Byte*>(req.laddr + offset), len},
    };
    tx_.cursor.emplace(iovs);
  } else {
    const std::array<IoVec, 2> iovs = {
        IoVec{tx_.header.data(), tx_.header.size()},
        IoVec{const_cast<Byte*>(req.laddr + offset), len},
    };
    tx_.cursor.emplace(iovs);
  }
}

void TcpChannelSession::prepareRxCursor() {
  // Point `rx_.cursor` at the destination buffer for the active RX phase.
  void* dst = nullptr;
  size_t len = 0;
  if (auto* hdr = std::get_if<RxState::ReadingHeader>(&rx_.phase)) {
    // Next 64-byte wire header.
    dst = hdr->buf.data();
    len = hdr->buf.size();
  } else if (auto* info = std::get_if<RxState::ReadingPostInfo>(&rx_.phase)) {
    // Pre-sized serialized `RequestInfo` payload.
    dst = info->payload.data();
    len = info->payload.size();
  } else if (auto* data = std::get_if<RxState::ReadingData>(&rx_.phase)) {
    // Zero-copy read directly into the resolved user destination buffer.
    DCHECK_NE(data->dst, nullptr);
    dst = data->dst;
    len = data->header.len;
  } else {
    // Discard unwanted payload bytes into the lazily allocated scratch buffer.
    auto& drain = std::get<RxState::Draining>(rx_.phase);
    if (rx_.drain_buf.empty()) rx_.drain_buf.resize(kDrainBufSize);
    dst = rx_.drain_buf.data();
    len = std::min(drain.remaining, kDrainBufSize);
  }
  const std::array<IoVec, 1> iov = {IoVec{dst, len}};
  rx_.cursor.emplace(iov);
}

bool TcpChannelSession::onHeader(const RxState::ReadingHeader& hdr) {
  // Dispatch by wire opcode; unknown/corrupt headers terminate the stream.
  const std::string_view raw(reinterpret_cast<const char*>(hdr.buf.data()),
                             hdr.buf.size());
  if (DataHeader chunk; HeaderUtil::Deserialize(raw, chunk)) {
    return onDataHeader(chunk);
  }
  if (PostInfoHeader info; HeaderUtil::Deserialize(raw, info)) {
    return onPostInfoHeader(info);
  }
  if (FailureHeader failure; HeaderUtil::Deserialize(raw, failure)) {
    if (!outgoing_.Fail(failure.handle, Status::kFailure, {},
                        &metrics_.write.errors)) {
      incoming_.Fail(failure.handle, Status::kFailure, remote_peer_);
    }
    return true;
  }
  LOG(WARNING) << "worker #" << worker_id_ << " invalid wire header on "
               << *channel_;
  return false;
}

bool TcpChannelSession::onDataHeader(const DataHeader& chunk) {
  // Zero-length DataHeader is an ACK for one of our outgoing chunks.
  if (chunk.IsAck()) {
    tracer_.RecordTxAckRcvd(chunk);
    outgoing_.Update(chunk.handle, chunk.reqid, chunk.index, traceTxWorkerId());
    return true;
  }

  // Validate wire length and offset before looking up destination memory.
  if (ABSL_PREDICT_FALSE(chunk.len > kChunkSize)) {
    LOG(WARNING) << "worker #" << worker_id_ << " oversized chunk " << chunk;
    return false;
  }
  const uint64_t expected_offset = uint64_t{chunk.index.value()} * kChunkSize;
  if (ABSL_PREDICT_FALSE(chunk.byte_offset != expected_offset)) {
    LOG(WARNING) << "worker #" << worker_id_ << " invalid chunk offset "
                 << chunk;
    return false;
  }

  const bool is_traced = chunk.trace.IsTraced();
  const uint64_t t_start_us = is_traced ? util::MonoNowUs() : 0;

  // Resolve destination buffer and tracker in one lookup; drain if unknown/OOB.
  const RequestTracker::ChunkTarget target = incoming_.FindChunk(
      chunk.handle, chunk.reqid, chunk.byte_offset, chunk.len, remote_peer_);
  if (ABSL_PREDICT_FALSE(target.dst == nullptr)) {
    LOG(WARNING) << "worker #" << worker_id_ << " unknown chunk " << chunk;
    rx_.phase.emplace<RxState::Draining>(
        RxState::Draining{.header = chunk, .remaining = chunk.len});
    return true;
  }

  // Acquire exclusive write ownership of this chunk slot; drain duplicates.
  ChunkTracker& tracker = *target.tracker;
  if (ABSL_PREDICT_FALSE(chunk.index.value() >= tracker.TotalNumChunks())) {
    LOG(WARNING) << "worker #" << worker_id_ << " out-of-range chunk index "
                 << chunk;
    return false;
  }

  const ChunkStatus status = tracker.Acquire(chunk.index);
  const uint64_t t_acquired_us = is_traced ? util::MonoNowUs() : 0;
  if (ABSL_PREDICT_TRUE(status == ChunkStatus::kEmpty)) {
    rx_.phase.emplace<RxState::ReadingData>(RxState::ReadingData{
        .header = chunk,
        .dst = target.dst,
        .tracker = &tracker,
        .t_start_us = t_start_us,
        .t_acquired_us = t_acquired_us,
    });
  } else {
    rx_.phase.emplace<RxState::Draining>(RxState::Draining{
        .header = chunk,
        .remaining = chunk.len,
        .ack_after = status == ChunkStatus::kDone,
        .t_start_us = t_start_us,
        .t_acquired_us = t_acquired_us,
    });
  }
  return true;
}

bool TcpChannelSession::onPostInfoHeader(const PostInfoHeader& info) {
  if (ABSL_PREDICT_FALSE(info.len == 0 ||
                         info.len > PostInfoHeader::kMaxPayloadBytes)) {
    LOG(WARNING) << "worker #" << worker_id_ << " invalid post info length "
                 << info;
    return false;
  }
  rx_.phase.emplace<RxState::ReadingPostInfo>(RxState::ReadingPostInfo{
      .header = info,
      .payload = std::string(info.len, '\0'),
  });
  return true;
}

void TcpChannelSession::onPayload() {
  if (auto* info = std::get_if<RxState::ReadingPostInfo>(&rx_.phase)) {
    // Register incoming post requests; notify sender on parse/resolve failure.
    std::vector<RequestInfo> reqs;
    if (ABSL_PREDICT_FALSE(!HeaderUtil::Deserialize(info->payload, reqs))) {
      LOG(WARNING) << "worker #" << worker_id_ << " invalid post info payload";
      tx_.control.AppendFailure(info->header.handle);
    } else if (!incoming_.Add(info->header.handle, reqs, util::MonoNowUs(),
                              info->header.metadata, remote_peer_)) {
      tx_.control.AppendFailure(info->header.handle);
    }
  } else if (auto* data = std::get_if<RxState::ReadingData>(&rx_.phase)) {
    // Release chunk slot, evict completed incoming entry, and queue ACK.
    DataHeader chunk = data->header;
    ChunkTracker* const tracker = data->tracker;
    const uint64_t t_start_us = data->t_start_us;
    const uint64_t t_acquired_us = data->t_acquired_us;
    rx_.phase.emplace<RxState::ReadingHeader>();
    if (tracker->Release(chunk.index, /*success=*/true)) {
      if (incoming_.OnReqComplete(chunk.handle, remote_peer_)) {
        incoming_.Remove(chunk.handle, remote_peer_);
      }
    }
    StampRxTrace(chunk, traceRxWorkerId(), ChunkStatus::kEmpty, t_start_us,
                 t_acquired_us);
    tx_.control.AppendAck(chunk);
    return;
  } else if (auto* drain = std::get_if<RxState::Draining>(&rx_.phase)) {
    // Advance drain counter; re-ACK if this was a completed duplicate chunk.
    drain->remaining -= std::min(drain->remaining, kDrainBufSize);
    if (drain->remaining > 0) return;
    if (drain->ack_after) {
      StampRxTrace(drain->header, traceRxWorkerId(), ChunkStatus::kDone,
                   drain->t_start_us, drain->t_acquired_us);
      tx_.control.AppendAck(drain->header);
    }
  }
  rx_.phase.emplace<RxState::ReadingHeader>();
}

}  // namespace peregrine::internal::engine2
