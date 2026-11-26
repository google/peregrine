#include "peregrine/src/internal/engine2/tcp_channel_session.h"

#include <sys/types.h>

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "absl/types/span.h"
#include "peregrine/src/api/transport_trace.h"
#include "peregrine/src/api/transport_types.h"
#include "peregrine/src/internal/base/endpoint.h"
#include "peregrine/src/internal/base/types.h"
#include "peregrine/src/internal/channel/channel.h"
#include "peregrine/src/internal/channel/channel_type.h"
#include "peregrine/src/internal/chunk/chunk_tracker.h"
#include "peregrine/src/internal/chunk/header.h"
#include "peregrine/src/internal/chunk/header_flatbuf.h"
#include "peregrine/src/internal/engine2/peer_work_queue.h"
#include "peregrine/src/internal/engine2/post.h"
#include "peregrine/src/internal/lib/iovec_cursor.h"
#include "peregrine/src/internal/metrics/engine_metrics.h"
#include "peregrine/src/internal/request/request_tracker.h"
#include "peregrine/src/internal/trace/trace_context.h"
#include "peregrine/src/internal/trace/trace_event.h"
#include "peregrine/src/internal/trace/trace_recorder.h"

namespace peregrine::internal::engine2 {
namespace {

using ::testing::ElementsAre;
using ::testing::IsEmpty;

// In-memory bidirectional byte pipe backing a pair of `FakeStreamChannel`s.
struct Pipe {
  std::deque<Byte> a_to_b;
  std::deque<Byte> b_to_a;
  size_t max_write = 1024 * 1024;
  size_t max_read = 1024 * 1024;
  bool fail_write = false;
  bool fail_read = false;
};

// Non-blocking in-memory stream `Channel` over one side of a `Pipe`.
class FakeStreamChannel final : public Channel {
 public:
  FakeStreamChannel(std::shared_ptr<Pipe> pipe, bool is_a)
      : pipe_(std::move(pipe)), is_a_(is_a) {}

  ChannelType Type() const override { return ChannelType::kMemStream; }
  bool IsBlocking() const override { return false; }

  ssize_t Write(IoVecCursor& iovecs) override {
    if (pipe_->fail_write) return -1;
    std::deque<Byte>& out = is_a_ ? pipe_->a_to_b : pipe_->b_to_a;
    size_t budget = pipe_->max_write;
    size_t written = 0;
    while (budget > 0 && iovecs.RemainingBytes() > 0) {
      const IoVec* cur = iovecs.Head();
      const size_t n = std::min(budget, cur->iov_len);
      const auto* bytes = static_cast<const Byte*>(cur->iov_base);
      out.insert(out.end(), bytes, bytes + n);
      iovecs.Advance(n);
      budget -= n;
      written += n;
    }
    return static_cast<ssize_t>(written);
  }

  ssize_t Read(IoVecCursor& iovecs) override {
    if (pipe_->fail_read) return -1;
    std::deque<Byte>& in = is_a_ ? pipe_->b_to_a : pipe_->a_to_b;
    size_t budget = std::min(pipe_->max_read, in.size());
    size_t read_bytes = 0;
    while (budget > 0 && iovecs.RemainingBytes() > 0) {
      const IoVec* cur = iovecs.Head();
      const size_t n = std::min(budget, cur->iov_len);
      auto* dst = static_cast<Byte*>(cur->iov_base);
      for (size_t i = 0; i < n; ++i) {
        dst[i] = in.front();
        in.pop_front();
      }
      iovecs.Advance(n);
      budget -= n;
      read_bytes += n;
    }
    return static_cast<ssize_t>(read_bytes);
  }

  void Shutdown() override {}
  std::string ToString() const override { return "fake_stream"; }

 private:
  std::shared_ptr<Pipe> pipe_;
  bool is_a_;
};

// Pumps `a` and `b` until both sides are idle with empty pipes.
void Pump(TcpChannelSession& a, TcpChannelSession& b, const Pipe& pipe) {
  for (int iter = 0; iter < 100000; ++iter) {
    bool a_more = false;
    bool b_more = false;
    bool a_rx_more = false;
    bool b_rx_more = false;
    a.ClearTxBlocked();
    b.ClearTxBlocked();
    ASSERT_TRUE(a.DriveTx(a_more));
    ASSERT_TRUE(b.DriveRx(b_rx_more));
    ASSERT_TRUE(b.DriveTx(b_more));
    ASSERT_TRUE(a.DriveRx(a_rx_more));
    if (!a_more && !b_more && !a_rx_more && !b_rx_more && pipe.a_to_b.empty() &&
        pipe.b_to_a.empty() && !a.HasPendingControl() &&
        !b.HasPendingControl()) {
      return;
    }
  }
  FAIL() << "Pump did not converge";
}

class TcpChannelSessionTest : public ::testing::Test {
 protected:
  TcpChannelSessionTest()
      : peer_a_(Endpoint::Create("127.0.0.1:10001")),
        peer_b_(Endpoint::Create("127.0.0.1:10002")),
        tracer_a_(TraceConfig{.sample_every_n = 0}),
        tracer_b_(TraceConfig{.sample_every_n = 0}),
        outgoing_a_(metrics_a_, tracer_a_),
        incoming_a_(metrics_a_, tracer_a_),
        outgoing_b_(metrics_b_, tracer_b_),
        incoming_b_(metrics_b_, tracer_b_, /*recv_post_callback=*/nullptr,
                    [this](const Metadata&,
                           const Metadata&) -> absl::Span<Byte> {
                      return resolve_dst_b_;
                    }) {}

  std::shared_ptr<QueuedPost> RegisterAndMakePost(
      RequestTracker& outgoing, Handle handle, Byte* src, Byte* dst, size_t len,
      RequestTracker::OnComplete on_complete = nullptr,
      TraceContext trace_ctx = {}) {
    const uint32_t nchunks = static_cast<uint32_t>((len - 1) / kChunkSize + 1);
    std::vector<RequestInfo> req_infos = {RequestInfo{
        .reqid = ReqId(1),
        .nchunks = nchunks,
        .raddr = addr_t(reinterpret_cast<uintptr_t>(dst)),
        .total_len = len,
        .op = Op::kWrite,
        .recipe = Recipe{.type = RecipeType::kContiguous, .params = {}},
    }};
    EXPECT_TRUE(outgoing.Add(handle, /*start_mono_us=*/0,
                             std::move(on_complete), trace_ctx));
    EXPECT_TRUE(outgoing.AddInitiatorReqs(handle, req_infos));
    auto qp = std::make_shared<QueuedPost>();
    qp->post.handle = handle;
    qp->post.trace_ctx = trace_ctx;
    qp->post.requests.push_back(
        Request{.op = Op::kWrite, .laddr = src, .raddr = dst, .len = len});
    qp->req_infos = std::move(req_infos);
    return qp;
  }

  PeerId peer_a_;
  PeerId peer_b_;
  EngineMetrics metrics_a_;
  EngineMetrics metrics_b_;
  TraceRecorder tracer_a_;
  TraceRecorder tracer_b_;
  absl::Span<Byte> resolve_dst_b_;
  RequestTracker outgoing_a_;
  RequestTracker incoming_a_;
  RequestTracker outgoing_b_;
  RequestTracker incoming_b_;
  PeerWorkQueue queue_a_;
  PeerWorkQueue queue_b_;
};

TEST_F(TcpChannelSessionTest, MultiChunkWriteTransfersDataAndEvictsIncoming) {
  auto pipe = std::make_shared<Pipe>();
  TcpChannelSession sender(std::make_unique<FakeStreamChannel>(pipe, true),
                           peer_a_, peer_b_, queue_a_, outgoing_a_, incoming_a_,
                           metrics_a_, tracer_a_, /*worker_id=*/0);
  TcpChannelSession receiver(std::make_unique<FakeStreamChannel>(pipe, false),
                             peer_b_, peer_a_, queue_b_, outgoing_b_,
                             incoming_b_, metrics_b_, tracer_b_,
                             /*worker_id=*/1);

  const size_t len = kChunkSize + 1234;
  std::vector<Byte> src(len);
  for (size_t i = 0; i < len; ++i) src[i] = static_cast<Byte>(i * 31 + 7);
  std::vector<Byte> dst(len, 0);
  resolve_dst_b_ = absl::MakeSpan(dst);

  Status done_status = Status::kInProgress;
  queue_a_.Enqueue(RegisterAndMakePost(outgoing_a_, Handle(42), src.data(),
                                       dst.data(), len,
                                       [&](Status s) { done_status = s; }));

  Pump(sender, receiver, *pipe);

  EXPECT_EQ(done_status, Status::kSuccess);
  EXPECT_EQ(dst, src);
  EXPECT_EQ(metrics_a_.write.bytes.Value(), len);
  EXPECT_EQ(metrics_a_.write.errors.Value(), 0);
  // Completed incoming tracker entry must be evicted.
  EXPECT_EQ(incoming_b_.Check(Handle(42), peer_a_), Status::kNotFound);
  EXPECT_THAT(tracer_a_.Buffer().Snapshot(), IsEmpty());
}

TEST_F(TcpChannelSessionTest, PartialWritesAndReadsWithBackpressure) {
  auto pipe = std::make_shared<Pipe>();
  pipe->max_write = 17;
  pipe->max_read = 13;

  TcpChannelSession sender(std::make_unique<FakeStreamChannel>(pipe, true),
                           peer_a_, peer_b_, queue_a_, outgoing_a_, incoming_a_,
                           metrics_a_, tracer_a_, /*worker_id=*/0);
  TcpChannelSession receiver(std::make_unique<FakeStreamChannel>(pipe, false),
                             peer_b_, peer_a_, queue_b_, outgoing_b_,
                             incoming_b_, metrics_b_, tracer_b_,
                             /*worker_id=*/1);

  std::vector<Byte> src(1024);
  for (size_t i = 0; i < src.size(); ++i) src[i] = static_cast<Byte>(i & 0xff);
  std::vector<Byte> dst(src.size(), 0);
  resolve_dst_b_ = absl::MakeSpan(dst);

  Status done_status = Status::kInProgress;
  queue_a_.Enqueue(RegisterAndMakePost(outgoing_a_, Handle(7), src.data(),
                                       dst.data(), src.size(),
                                       [&](Status s) { done_status = s; }));

  // First DriveTx should hit backpressure because max_write=17 < 64B header.
  bool has_more = false;
  ASSERT_TRUE(sender.DriveTx(has_more));
  EXPECT_TRUE(sender.IsTxBlocked());
  EXPECT_FALSE(has_more);

  Pump(sender, receiver, *pipe);

  EXPECT_EQ(done_status, Status::kSuccess);
  EXPECT_EQ(dst, src);
}

TEST_F(TcpChannelSessionTest, UnresolvedDestinationFailsSender) {
  auto pipe = std::make_shared<Pipe>();
  TcpChannelSession sender(std::make_unique<FakeStreamChannel>(pipe, true),
                           peer_a_, peer_b_, queue_a_, outgoing_a_, incoming_a_,
                           metrics_a_, tracer_a_, /*worker_id=*/0);
  TcpChannelSession receiver(std::make_unique<FakeStreamChannel>(pipe, false),
                             peer_b_, peer_a_, queue_b_, outgoing_b_,
                             incoming_b_, metrics_b_, tracer_b_,
                             /*worker_id=*/1);

  std::vector<Byte> src(256, 0xAB);
  Status done_status = Status::kInProgress;
  // Null destination address without resolve_addr_callback fails on receiver.
  queue_a_.Enqueue(RegisterAndMakePost(outgoing_a_, Handle(99), src.data(),
                                       /*dst=*/nullptr, src.size(),
                                       [&](Status s) { done_status = s; }));

  Pump(sender, receiver, *pipe);

  EXPECT_EQ(done_status, Status::kFailure);
  EXPECT_EQ(metrics_a_.write.errors.Value(), 1);
}

TEST_F(TcpChannelSessionTest, RejectsInvalidAndOversizedHeaders) {
  {
    auto pipe = std::make_shared<Pipe>();
    TcpChannelSession receiver(std::make_unique<FakeStreamChannel>(pipe, false),
                               peer_b_, peer_a_, queue_b_, outgoing_b_,
                               incoming_b_, metrics_b_, tracer_b_,
                               /*worker_id=*/0);
    // 64 bytes of garbage fail header deserialization.
    pipe->a_to_b.insert(pipe->a_to_b.end(), HeaderUtil::kSize, 0xff);
    bool has_more = false;
    EXPECT_FALSE(receiver.DriveRx(has_more));
  }
  {
    auto pipe = std::make_shared<Pipe>();
    TcpChannelSession receiver(std::make_unique<FakeStreamChannel>(pipe, false),
                               peer_b_, peer_a_, queue_b_, outgoing_b_,
                               incoming_b_, metrics_b_, tracer_b_,
                               /*worker_id=*/0);
    // Oversized PostInfoHeader (> 4 MiB) is rejected before allocating.
    PostInfoHeader big = {};
    big.opcode = Opcode::kPostInfo;
    big.handle = Handle(1);
    big.len = 8 * 1024 * 1024;
    big.peer = peer_a_;
    const std::string raw = HeaderUtil::Serialize(big);
    pipe->a_to_b.insert(pipe->a_to_b.end(), raw.begin(), raw.end());
    bool has_more = false;
    EXPECT_FALSE(receiver.DriveRx(has_more));
  }
}

TEST_F(TcpChannelSessionTest, DrainsChunkExceedingRequestTotalLenSafely) {
  auto pipe = std::make_shared<Pipe>();
  TcpChannelSession receiver(std::make_unique<FakeStreamChannel>(pipe, false),
                             peer_b_, peer_a_, queue_b_, outgoing_b_,
                             incoming_b_, metrics_b_, tracer_b_,
                             /*worker_id=*/0);

  // Register a 16-byte destination buffer with a sentinel guard byte after it.
  std::vector<Byte> dst(32, 0x5A);
  resolve_dst_b_ = absl::MakeSpan(dst);
  const std::vector<RequestInfo> reqs = {RequestInfo{
      .reqid = ReqId(1),
      .nchunks = 1,
      .raddr = addr_t(reinterpret_cast<uintptr_t>(dst.data())),
      .total_len = 16,
      .op = Op::kWrite,
      .recipe = Recipe{.type = RecipeType::kContiguous, .params = {}},
  }};
  ASSERT_TRUE(incoming_b_.Add(Handle(5), reqs, /*start_us=*/0, {}, peer_a_));

  // Malicious chunk claims len=32 (> total_len=16). Must be drained, not
  // written.
  DataHeader bad = {};
  bad.opcode = Opcode::kData;
  bad.handle = Handle(5);
  bad.len = 32;
  bad.reqid = ReqId(1);
  bad.index = chunk_t(0);
  bad.byte_offset = 0;
  const std::string hdr = HeaderUtil::Serialize(bad);
  pipe->a_to_b.insert(pipe->a_to_b.end(), hdr.begin(), hdr.end());
  pipe->a_to_b.insert(pipe->a_to_b.end(), 32, 0xEE);

  bool has_more = false;
  ASSERT_TRUE(receiver.DriveRx(has_more));
  EXPECT_FALSE(has_more);
  EXPECT_TRUE(pipe->a_to_b.empty());
  EXPECT_FALSE(receiver.HasPendingControl());  // no ACK sent for invalid chunk
  for (const Byte b : dst) EXPECT_EQ(b, 0x5A);
}

TEST_F(TcpChannelSessionTest, DestructionFailsActiveOutgoingPost) {
  auto pipe = std::make_shared<Pipe>();
  pipe->max_write = 16;  // stall mid-frame

  std::vector<Byte> src(128, 1);
  std::vector<Byte> dst(128, 0);
  Status done_status = Status::kInProgress;
  queue_a_.Enqueue(RegisterAndMakePost(outgoing_a_, Handle(11), src.data(),
                                       dst.data(), src.size(),
                                       [&](Status s) { done_status = s; }));

  {
    TcpChannelSession sender(std::make_unique<FakeStreamChannel>(pipe, true),
                             peer_a_, peer_b_, queue_a_, outgoing_a_,
                             incoming_a_, metrics_a_, tracer_a_,
                             /*worker_id=*/0);
    bool has_more = false;
    ASSERT_TRUE(sender.DriveTx(has_more));
    EXPECT_TRUE(sender.IsTxBlocked());
  }
  EXPECT_EQ(done_status, Status::kFailure);
  EXPECT_EQ(metrics_a_.write.errors.Value(), 1);
}

TEST_F(TcpChannelSessionTest, DriveRxYieldsAtReadBudget) {
  auto pipe = std::make_shared<Pipe>();
  TcpChannelSession receiver(std::make_unique<FakeStreamChannel>(pipe, false),
                             peer_b_, peer_a_, queue_b_, outgoing_b_,
                             incoming_b_, metrics_b_, tracer_b_,
                             /*worker_id=*/0);

  // Queue 40 valid ACK headers (> kReadBudget = 32).
  DataHeader ack = {};
  ack.opcode = Opcode::kData;
  ack.handle = Handle(77);
  ack.len = 0;
  ack.reqid = ReqId(1);
  ack.index = chunk_t(0);
  const std::string raw = HeaderUtil::Serialize(ack);
  for (int i = 0; i < 40; ++i) {
    pipe->a_to_b.insert(pipe->a_to_b.end(), raw.begin(), raw.end());
  }

  bool has_more = false;
  ASSERT_TRUE(receiver.DriveRx(has_more));
  EXPECT_TRUE(has_more);
  EXPECT_FALSE(pipe->a_to_b.empty());

  ASSERT_TRUE(receiver.DriveRx(has_more));
  EXPECT_FALSE(has_more);
  EXPECT_TRUE(pipe->a_to_b.empty());
}

TEST_F(TcpChannelSessionTest, DriveRxClosesOnTruncatedFrameBeforePeerClose) {
  auto pipe = std::make_shared<Pipe>();
  TcpChannelSession receiver(std::make_unique<FakeStreamChannel>(pipe, false),
                             peer_b_, peer_a_, queue_b_, outgoing_b_,
                             incoming_b_, metrics_b_, tracer_b_,
                             /*worker_id=*/0);

  // Peer writes a truncated header (16 of 64 bytes) and closes cleanly (FIN).
  pipe->a_to_b.assign(16, 0x42);

  bool has_more = false;
  EXPECT_FALSE(receiver.DriveRx(has_more, /*peer_closed=*/true));
  EXPECT_FALSE(has_more);
  EXPECT_TRUE(pipe->a_to_b.empty());
}

TEST_F(TcpChannelSessionTest, TracedChunkRecordsTxRxAndAckStages) {
  auto pipe = std::make_shared<Pipe>();
  TcpChannelSession sender(std::make_unique<FakeStreamChannel>(pipe, true),
                           peer_a_, peer_b_, queue_a_, outgoing_a_, incoming_a_,
                           metrics_a_, tracer_a_, /*worker_id=*/0);
  TcpChannelSession receiver(std::make_unique<FakeStreamChannel>(pipe, false),
                             peer_b_, peer_a_, queue_b_, outgoing_b_,
                             incoming_b_, metrics_b_, tracer_b_,
                             /*worker_id=*/1);

  std::vector<Byte> src(1024, 0x5C);
  std::vector<Byte> dst(src.size(), 0);
  resolve_dst_b_ = absl::MakeSpan(dst);
  Status done_status = Status::kInProgress;
  queue_a_.Enqueue(RegisterAndMakePost(
      outgoing_a_, Handle(55), src.data(), dst.data(), src.size(),
      [&](Status s) { done_status = s; }, TraceContext(/*is_traced=*/true)));

  Pump(sender, receiver, *pipe);
  EXPECT_EQ(done_status, Status::kSuccess);
  EXPECT_EQ(dst, src);

  const std::vector<TraceEvent> events = tracer_a_.Buffer().Snapshot();
  std::vector<TraceStage> stages;
  stages.reserve(events.size());
  for (const TraceEvent& ev : events) {
    EXPECT_EQ(ev.handle, 55);
    stages.push_back(ev.stage);
    if (ev.stage == TraceStage::kWorkerDequeued ||
        ev.stage == TraceStage::kTxWriteDone) {
      EXPECT_EQ(ev.worker_id, 1);  // 1-based TX worker ID for worker_id=0
    } else if (ev.stage == TraceStage::kTxAckRcvd) {
      EXPECT_EQ(ev.worker_id, -2);  // negative 1-based RX ID for worker_id=1
      EXPECT_EQ(ev.metadata,
                static_cast<uint8_t>(ChunkTracker::ChunkStatus::kEmpty));
    } else if (ev.stage == TraceStage::kRequestDone ||
               ev.stage == TraceStage::kAllRequestsDone ||
               ev.stage == TraceStage::kCallbackDone) {
      EXPECT_EQ(ev.worker_id, 1);
    }
  }
  EXPECT_THAT(
      stages,
      ElementsAre(TraceStage::kWorkerDequeued, TraceStage::kTxWriteDone,
                  TraceStage::kTxAckRcvd, TraceStage::kRequestDone,
                  TraceStage::kAllRequestsDone, TraceStage::kCallbackDone));
}

TEST_F(TcpChannelSessionTest, TracedDuplicateChunkStampsDoneStatusInReAck) {
  auto pipe = std::make_shared<Pipe>();
  TcpChannelSession receiver(std::make_unique<FakeStreamChannel>(pipe, false),
                             peer_b_, peer_a_, queue_b_, outgoing_b_,
                             incoming_b_, metrics_b_, tracer_b_,
                             /*worker_id=*/2);

  // Register a 2-chunk request so chunk 0 completion does not evict the entry.
  std::vector<Byte> dst(kChunkSize + 64, 0);
  resolve_dst_b_ = absl::MakeSpan(dst);
  const std::vector<RequestInfo> reqs = {RequestInfo{
      .reqid = ReqId(1),
      .nchunks = 2,
      .raddr = addr_t(reinterpret_cast<uintptr_t>(dst.data())),
      .total_len = dst.size(),
      .op = Op::kWrite,
      .recipe = Recipe{.type = RecipeType::kContiguous, .params = {}},
  }};
  ASSERT_TRUE(incoming_b_.Add(Handle(66), reqs, /*start_us=*/0, {}, peer_a_));
  // Mark chunk 0 already completed so an incoming chunk 0 is a duplicate.
  ChunkTracker& tracker = incoming_b_.Find(Handle(66), ReqId(1), peer_a_);
  ASSERT_EQ(tracker.Acquire(chunk_t(0)), ChunkTracker::ChunkStatus::kEmpty);
  ASSERT_FALSE(tracker.Release(chunk_t(0), /*success=*/true));

  DataHeader dup = {};
  dup.opcode = Opcode::kData;
  dup.handle = Handle(66);
  dup.len = 64;
  dup.reqid = ReqId(1);
  dup.index = chunk_t(0);
  dup.byte_offset = 0;
  dup.trace.SetTraced();
  const std::string hdr = HeaderUtil::Serialize(dup);
  pipe->a_to_b.insert(pipe->a_to_b.end(), hdr.begin(), hdr.end());
  pipe->a_to_b.insert(pipe->a_to_b.end(), 64, 0x33);

  bool has_more = false;
  ASSERT_TRUE(receiver.DriveRx(has_more));
  ASSERT_TRUE(receiver.HasPendingControl());
  ASSERT_TRUE(receiver.DriveTx(has_more));
  ASSERT_EQ(pipe->b_to_a.size(), HeaderUtil::kSize);

  const std::string raw_ack(pipe->b_to_a.begin(), pipe->b_to_a.end());
  DataHeader ack = {};
  ASSERT_TRUE(HeaderUtil::Deserialize(raw_ack, ack));
  EXPECT_TRUE(ack.IsAck());
  EXPECT_TRUE(ack.trace.IsTraced());
  EXPECT_EQ(ack.trace.rx_worker_id, -3);  // negative 1-based for worker_id=2
  EXPECT_EQ(ack.trace.rx_chunk_status,
            static_cast<uint8_t>(ChunkTracker::ChunkStatus::kDone));
}

}  // namespace
}  // namespace peregrine::internal::engine2
