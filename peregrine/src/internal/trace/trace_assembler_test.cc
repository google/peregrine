#include "peregrine/src/internal/trace/trace_assembler.h"

#include <cstdint>
#include <vector>

#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "absl/types/span.h"
#include "peregrine/src/api/transport_trace.h"
#include "peregrine/src/api/transport_types.h"
#include "peregrine/src/internal/base/types.h"
#include "peregrine/src/internal/trace/trace_event.h"

namespace peregrine::internal::testing {
namespace {

using ::testing::IsEmpty;
using ::testing::SizeIs;

constexpr uint32_t kHandle = 0x1001;
constexpr uint32_t kReqId1 = 0x2001;
constexpr uint32_t kReqId2 = 0x2002;

// A traced chunk and the sender worker it is enqueued to.
struct TracedChunk {
  uint32_t handle = kHandle;
  uint32_t reqid = 0;
  uint32_t index = 0;
  int16_t send_worker_id = 1;
  uint32_t size_bytes = 4096;
};

constexpr TracedChunk kChunk1 = {.reqid = kReqId1};
constexpr TracedChunk kChunk2 = {.reqid = kReqId2};

// Receiver timings of a chunk, piggybacked on its ACK.
struct RxTimings {
  int16_t worker_id = -1;
  uint32_t payload_us = 0;
  uint32_t acquire_us = 0;
  uint8_t chunk_status = 0;  // kEmpty
};

// The Record*() methods append events encoded like the TraceRecorder method of
// the same name, but with explicit timestamps.
class TraceAssemblerTest : public ::testing::Test {
 protected:
  void RecordEngineEnqueued(uint32_t handle, uint64_t t_entry_us,
                            uint64_t t_enqueued_us, uint32_t num_requests,
                            Op op) {
    events_.push_back({.timestamp_us = t_entry_us,
                       .handle = handle,
                       .stage = TraceStage::kPostEntry,
                       .metadata = static_cast<uint8_t>(op),
                       .metric1 = num_requests});
    events_.push_back({.timestamp_us = t_enqueued_us,
                       .handle = handle,
                       .stage = TraceStage::kEngineEnqueued,
                       .metadata = static_cast<uint8_t>(op),
                       .metric1 = num_requests});
  }

  void RecordConnSetupStart(uint32_t handle, uint64_t t_us,
                            uint32_t num_conns) {
    RecordConnSetupStage(handle, TraceStage::kConnSetupStart, t_us, num_conns);
  }

  void RecordGetPeerHostInfoDone(uint32_t handle, uint64_t t_us) {
    RecordConnSetupStage(handle, TraceStage::kGetPeerHostInfoDone, t_us, 0);
  }

  void RecordConnSetupDone(uint32_t handle, uint64_t t_us,
                           uint32_t num_workers) {
    RecordConnSetupStage(handle, TraceStage::kConnSetupDone, t_us, num_workers);
  }

  void RecordEngineDequeued(uint32_t handle, uint32_t reqid, uint64_t t_us,
                            Op op, uint32_t len) {
    events_.push_back({.timestamp_us = t_us,
                       .handle = handle,
                       .reqid = reqid,
                       .stage = TraceStage::kEngineDequeued,
                       .metadata = static_cast<uint8_t>(op),
                       .metric1 = len});
  }

  void RecordWorkerEnqueued(const TracedChunk& c, uint64_t t_us) {
    RecordSendStage(c, TraceStage::kWorkerEnqueued, t_us);
  }

  void RecordWorkerDequeued(const TracedChunk& c, uint64_t t_us) {
    RecordSendStage(c, TraceStage::kWorkerDequeued, t_us);
  }

  void RecordTxWriteDone(const TracedChunk& c, uint64_t t_us) {
    RecordSendStage(c, TraceStage::kTxWriteDone, t_us);
  }

  void RecordTxAckRcvd(const TracedChunk& c, uint64_t t_us,
                       const RxTimings& rx) {
    events_.push_back({.timestamp_us = t_us,
                       .handle = c.handle,
                       .reqid = c.reqid,
                       .stage = TraceStage::kTxAckRcvd,
                       .metadata = rx.chunk_status,
                       .worker_id = rx.worker_id,
                       .chunk_idx = c.index,
                       .metric1 = rx.payload_us,
                       .metric2 = rx.acquire_us});
  }

  void RecordAllChunksDone(const TracedChunk& c, uint64_t t_us) {
    events_.push_back({.timestamp_us = t_us,
                       .handle = c.handle,
                       .reqid = c.reqid,
                       .stage = TraceStage::kAllChunksDone,
                       .worker_id = c.send_worker_id,
                       .chunk_idx = c.index});
  }

  void RecordCallbackDone(uint32_t handle, uint64_t t_us, int16_t worker_id) {
    events_.push_back({.timestamp_us = t_us,
                       .handle = handle,
                       .stage = TraceStage::kCallbackDone,
                       .worker_id = worker_id});
  }

  void RecordPollReaped(uint32_t handle, uint64_t t_us) {
    events_.push_back({.timestamp_us = t_us,
                       .handle = handle,
                       .stage = TraceStage::kPollReaped});
  }

  // Records a cold-start Post() of two single-chunk requests on one worker,
  // reaped by Poll(), in the order that Engine and Worker record them.
  void RecordColdStartPolledPost() {
    RecordEngineEnqueued(kHandle, 1000, 1018, /*num_requests=*/2, Op::kWrite);
    RecordConnSetupStart(kHandle, 1020, /*num_conns=*/1);
    RecordGetPeerHostInfoDone(kHandle, 1200);
    RecordConnSetupDone(kHandle, 1450, /*num_workers=*/1);
    RecordEngineDequeued(kHandle, kReqId1, 1455, Op::kWrite, /*len=*/4096);
    RecordEngineDequeued(kHandle, kReqId2, 1456, Op::kWrite, /*len=*/4096);
    RecordWorkerEnqueued(kChunk1, 1460);
    RecordWorkerEnqueued(kChunk2, 1465);
    RecordWorkerDequeued(kChunk1, 1470);
    RecordTxWriteDone(kChunk1, 1690);
    RecordWorkerDequeued(kChunk2, 1700);
    RecordTxAckRcvd(kChunk1, 1750, {.payload_us = 200, .acquire_us = 5});
    RecordTxWriteDone(kChunk2, 1880);
    RecordTxAckRcvd(kChunk2, 1950, {.payload_us = 190, .acquire_us = 4});
    RecordAllChunksDone(kChunk2, 1955);
    RecordPollReaped(kHandle, 2000);
  }

  // Records a Post() of a single-chunk request to a connected peer, starting
  // at t_us and completed by on_complete.
  void RecordWarmCallbackPost(uint32_t handle, uint64_t t_us) {
    const TracedChunk chunk = {
        .handle = handle, .reqid = kReqId1, .size_bytes = 1024};
    RecordEngineEnqueued(handle, t_us, t_us + 10, /*num_requests=*/1,
                         Op::kWrite);
    RecordEngineDequeued(handle, kReqId1, t_us + 30, Op::kWrite,
                         /*len=*/1024);
    RecordWorkerEnqueued(chunk, t_us + 35);
    RecordWorkerDequeued(chunk, t_us + 40);
    RecordTxWriteDone(chunk, t_us + 50);
    RecordTxAckRcvd(chunk, t_us + 70, {.payload_us = 8, .acquire_us = 2});
    RecordAllChunksDone(chunk, t_us + 71);
    RecordCallbackDone(handle, t_us + 80, /*worker_id=*/1);
  }

  std::vector<TraceEvent> events_;

 private:
  void RecordConnSetupStage(uint32_t handle, TraceStage stage, uint64_t t_us,
                            uint32_t metric1) {
    events_.push_back({.timestamp_us = t_us,
                       .handle = handle,
                       .stage = stage,
                       .metric1 = metric1});
  }

  void RecordSendStage(const TracedChunk& c, TraceStage stage, uint64_t t_us) {
    events_.push_back({.timestamp_us = t_us,
                       .handle = c.handle,
                       .reqid = c.reqid,
                       .stage = stage,
                       .worker_id = c.send_worker_id,
                       .chunk_idx = c.index,
                       .metric1 = c.size_bytes});
  }
};

TEST_F(TraceAssemblerTest, ColdStartPolledPost) {
  RecordColdStartPolledPost();

  const std::vector<PostTraceRecord> records =
      TraceAssembler::Assemble(events_);
  ASSERT_THAT(records, SizeIs(1));
  const PostTraceRecord& rec = records[0];
  EXPECT_EQ(rec.handle, Handle(kHandle));
  EXPECT_EQ(rec.post_start_us, 1000);
  EXPECT_EQ(rec.enqueue_overhead_us, 18);
  EXPECT_EQ(rec.engine_queue_wait_us, 2);
  EXPECT_EQ(rec.conn_setup_us, 430);
  EXPECT_EQ(rec.transport_e2e_us, 955);
  EXPECT_EQ(rec.completion_pickup_us, 45);
  EXPECT_EQ(rec.completion_mode, CompletionMode::kPoll);
  ASSERT_THAT(rec.requests, SizeIs(2));

  const RequestTraceSummary& r1 = rec.requests[0];
  EXPECT_EQ(r1.reqid, kReqId1);
  EXPECT_EQ(r1.op, Op::kWrite);
  EXPECT_EQ(r1.len, 4096);
  EXPECT_EQ(r1.chunk_fanout_us, 5);
  ASSERT_THAT(r1.chunks, SizeIs(1));
  const ChunkTraceSummary& c1 = r1.chunks[0];
  EXPECT_EQ(c1.chunk_index, 0);
  EXPECT_EQ(c1.size_bytes, 4096);
  EXPECT_EQ(c1.send_worker_id, 1);
  EXPECT_EQ(c1.recv_worker_id, -1);
  EXPECT_EQ(c1.worker_queue_wait_us, 10);
  EXPECT_EQ(c1.tx_write_us, 220);
  EXPECT_EQ(c1.rx_acquire_wait_us, 5);
  EXPECT_EQ(c1.rx_payload_write_us, 200);
  EXPECT_EQ(c1.chunk_rtt_us, 280);
  EXPECT_EQ(c1.rx_chunk_status, 0);

  const RequestTraceSummary& r2 = rec.requests[1];
  EXPECT_EQ(r2.reqid, kReqId2);
  // Includes the fan-out of kReqId1, which is dequeued first.
  EXPECT_EQ(r2.chunk_fanout_us, 9);
  ASSERT_THAT(r2.chunks, SizeIs(1));
  // Includes head-of-line blocking behind the SendChunk of kReqId1.
  EXPECT_EQ(r2.chunks[0].worker_queue_wait_us, 235);
  EXPECT_EQ(r2.chunks[0].tx_write_us, 180);
  EXPECT_EQ(r2.chunks[0].chunk_rtt_us, 250);
}

TEST_F(TraceAssemblerTest, WarmCallbackPost) {
  RecordWarmCallbackPost(kHandle, 100);

  const std::vector<PostTraceRecord> records =
      TraceAssembler::Assemble(events_);
  ASSERT_THAT(records, SizeIs(1));
  const PostTraceRecord& rec = records[0];
  // Without connection setup, the engine queue wait ends at T_EngineDequeued.
  EXPECT_EQ(rec.engine_queue_wait_us, 20);
  EXPECT_EQ(rec.conn_setup_us, 0);
  EXPECT_EQ(rec.transport_e2e_us, 71);
  EXPECT_EQ(rec.completion_pickup_us, 9);
  EXPECT_EQ(rec.completion_mode, CompletionMode::kCallback);
}

TEST_F(TraceAssemblerTest, ReadPostHasNoChunks) {
  RecordEngineEnqueued(kHandle, 100, 110, /*num_requests=*/1, Op::kRead);
  RecordEngineDequeued(kHandle, kReqId1, 120, Op::kRead, /*len=*/64);
  RecordAllChunksDone({.reqid = kReqId1, .send_worker_id = 0}, 121);
  RecordPollReaped(kHandle, 130);

  const std::vector<PostTraceRecord> records =
      TraceAssembler::Assemble(events_);
  ASSERT_THAT(records, SizeIs(1));
  EXPECT_EQ(records[0].transport_e2e_us, 21);
  ASSERT_THAT(records[0].requests, SizeIs(1));
  const RequestTraceSummary& request = records[0].requests[0];
  EXPECT_EQ(request.op, Op::kRead);
  EXPECT_EQ(request.len, 64);
  EXPECT_THAT(request.chunks, IsEmpty());
}

TEST_F(TraceAssemblerTest, SortsPostsRequestsAndChunks) {
  constexpr uint32_t kLaterHandle = 0x1000;
  RecordWarmCallbackPost(kLaterHandle, 5000);
  RecordEngineEnqueued(kHandle, 100, 110, /*num_requests=*/2, Op::kWrite);
  RecordEngineDequeued(kHandle, kReqId2, 120, Op::kWrite, /*len=*/10);
  RecordEngineDequeued(kHandle, kReqId1, 121, Op::kWrite, /*len=*/30);
  for (const uint32_t index : {2u, 0u, 1u}) {
    RecordWorkerEnqueued({.reqid = kReqId1,
                          .index = index,
                          .send_worker_id = static_cast<int16_t>(index + 1),
                          .size_bytes = 10},
                         130 + index);
  }

  const std::vector<PostTraceRecord> records =
      TraceAssembler::Assemble(events_);
  ASSERT_THAT(records, SizeIs(2));
  EXPECT_EQ(records[0].handle, Handle(kHandle));
  EXPECT_EQ(records[1].handle, Handle(kLaterHandle));
  const std::vector<RequestTraceSummary>& requests = records[0].requests;
  ASSERT_THAT(requests, SizeIs(2));
  EXPECT_EQ(requests[0].reqid, kReqId2);
  EXPECT_EQ(requests[0].len, 10);
  EXPECT_EQ(requests[1].reqid, kReqId1);
  EXPECT_EQ(requests[1].len, 30);
  EXPECT_EQ(requests[1].chunk_fanout_us, 11);
  ASSERT_THAT(requests[1].chunks, SizeIs(3));
  for (uint32_t i = 0; i < 3; ++i) {
    EXPECT_EQ(requests[1].chunks[i].chunk_index, i);
    EXPECT_EQ(requests[1].chunks[i].send_worker_id,
              static_cast<int16_t>(i + 1));
  }
}

TEST_F(TraceAssemblerTest, KeepsEarliestRepeatedStages) {
  RecordColdStartPolledPost();
  // A duplicate chunk of kReqId1 is ACKed again with kDone, which also
  // re-records kAllChunksDone before Poll() reaps the handle.
  RecordTxAckRcvd(kChunk1, 1960,
                  {.payload_us = 1, .acquire_us = 1, .chunk_status = 2});
  RecordAllChunksDone(kChunk1, 1965);

  const std::vector<PostTraceRecord> records =
      TraceAssembler::Assemble(events_);
  ASSERT_THAT(records, SizeIs(1));
  EXPECT_EQ(records[0].transport_e2e_us, 955);
  ASSERT_THAT(records[0].requests, SizeIs(2));
  ASSERT_THAT(records[0].requests[0].chunks, SizeIs(1));
  const ChunkTraceSummary& chunk = records[0].requests[0].chunks[0];
  EXPECT_EQ(chunk.chunk_rtt_us, 280);
  EXPECT_EQ(chunk.rx_payload_write_us, 200);
  EXPECT_EQ(chunk.rx_chunk_status, 0);
}

TEST_F(TraceAssemblerTest, UncapturedOrInvertedStagesHaveZeroDurations) {
  RecordEngineEnqueued(kHandle, 1000, 1010, /*num_requests=*/1, Op::kWrite);
  RecordEngineDequeued(kHandle, kReqId1, 1020, Op::kWrite, /*len=*/4096);
  RecordWorkerEnqueued(kChunk1, 1100);
  RecordWorkerDequeued(kChunk1, 1050);

  const std::vector<PostTraceRecord> records =
      TraceAssembler::Assemble(events_);
  ASSERT_THAT(records, SizeIs(1));
  const PostTraceRecord& rec = records[0];
  EXPECT_EQ(rec.transport_e2e_us, 0);
  EXPECT_EQ(rec.completion_pickup_us, 0);
  EXPECT_EQ(rec.completion_mode, CompletionMode::kUnknown);
  ASSERT_THAT(rec.requests, SizeIs(1));
  ASSERT_THAT(rec.requests[0].chunks, SizeIs(1));
  const ChunkTraceSummary& chunk = rec.requests[0].chunks[0];
  EXPECT_EQ(chunk.worker_queue_wait_us, 0);
  EXPECT_EQ(chunk.tx_write_us, 0);
  EXPECT_EQ(chunk.chunk_rtt_us, 0);
  EXPECT_EQ(chunk.recv_worker_id, 0);
}

TEST_F(TraceAssemblerTest, IgnoresInvalidEvents) {
  RecordWarmCallbackPost(kHandle, 100);
  // A cleared slot, and events without a valid handle, ReqId, or chunk.
  events_.push_back({.handle = kHandle, .stage = TraceStage::kAllChunksDone});
  events_.push_back({.timestamp_us = 150,
                     .handle = kInvalidHandle.value(),
                     .stage = TraceStage::kPostEntry});
  events_.push_back({.timestamp_us = 150,
                     .handle = kHandle,
                     .reqid = kInvalidReqId.value(),
                     .stage = TraceStage::kEngineDequeued});
  events_.push_back({.timestamp_us = 150,
                     .handle = kHandle,
                     .reqid = kReqId2,
                     .stage = TraceStage::kWorkerEnqueued});

  const std::vector<PostTraceRecord> records =
      TraceAssembler::Assemble(events_);
  ASSERT_THAT(records, SizeIs(1));
  EXPECT_EQ(records[0].transport_e2e_us, 71);
  ASSERT_THAT(records[0].requests, SizeIs(1));
  EXPECT_THAT(records[0].requests[0].chunks, SizeIs(1));
}

TEST_F(TraceAssemblerTest, SkipsPostsWithoutPostEntry) {
  // The ring buffer overwrote the earlier events of this Post().
  RecordWorkerDequeued(kChunk1, 1470);
  RecordTxWriteDone(kChunk1, 1690);
  RecordTxAckRcvd(kChunk1, 1750, {.payload_us = 200});
  RecordAllChunksDone(kChunk1, 1755);

  EXPECT_THAT(TraceAssembler::Assemble(events_), IsEmpty());
}

TEST_F(TraceAssemblerTest, EmptySnapshot) {
  EXPECT_THAT(TraceAssembler::Assemble({}), IsEmpty());
}

TEST_F(TraceAssemblerTest, AssembleTimelinesKeepsStageTimestamps) {
  RecordColdStartPolledPost();

  const std::vector<PostTimeline> timelines =
      TraceAssembler::AssembleTimelines(events_);
  ASSERT_THAT(timelines, SizeIs(1));
  const PostTimeline& t = timelines[0];
  EXPECT_EQ(t.handle, Handle(kHandle));
  EXPECT_EQ(t.t_entry_us, 1000);
  EXPECT_EQ(t.t_enqueued_us, 1018);
  EXPECT_EQ(t.t_conn_start_us, 1020);
  EXPECT_EQ(t.t_host_info_done_us, 1200);
  EXPECT_EQ(t.t_conn_done_us, 1450);
  EXPECT_EQ(t.num_channels, 1);
  EXPECT_EQ(t.t_all_done_us, 1955);
  EXPECT_EQ(t.all_done_worker_id, 1);
  EXPECT_EQ(t.t_completed_us, 2000);
  EXPECT_EQ(t.completion_mode, CompletionMode::kPoll);
  ASSERT_THAT(t.requests, SizeIs(2));
  EXPECT_EQ(t.requests[0].t_dequeued_us, 1455);
  EXPECT_EQ(t.requests[1].t_dequeued_us, 1456);
  ASSERT_THAT(t.requests[1].chunks, SizeIs(1));
  const ChunkTimeline& c = t.requests[1].chunks[0];
  EXPECT_EQ(c.t_enqueued_us, 1465);
  EXPECT_EQ(c.t_dequeued_us, 1700);
  EXPECT_EQ(c.t_tx_done_us, 1880);
  EXPECT_EQ(c.t_ack_us, 1950);
  EXPECT_EQ(c.recv_worker_id, -1);
  EXPECT_EQ(c.rx_acquire_us, 4);
  EXPECT_EQ(c.rx_payload_us, 190);
}

TEST(ElapsedTest, IsZeroUnlessBothStagesAreCapturedInOrder) {
  EXPECT_EQ(Elapsed(3, 5), 2);
  EXPECT_EQ(Elapsed(0, 5), 0);
  EXPECT_EQ(Elapsed(5, 0), 0);
  EXPECT_EQ(Elapsed(5, 5), 0);
  EXPECT_EQ(Elapsed(5, 3), 0);
}

TEST(EngineStartUsTest, IsConnSetupStartOrFirstDequeue) {
  PostTimeline t = {.requests = {{.t_dequeued_us = 30}, {.t_dequeued_us = 31}}};
  EXPECT_EQ(EngineStartUs(t), 30);
  t.t_conn_start_us = 20;
  EXPECT_EQ(EngineStartUs(t), 20);
  EXPECT_EQ(EngineStartUs(PostTimeline()), 0);
}

}  // namespace
}  // namespace peregrine::internal::testing
