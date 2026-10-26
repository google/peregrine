#include "peregrine/src/internal/trace/trace_recorder.h"

#include <vector>

#include "gtest/gtest.h"
#include "peregrine/src/api/trace/transport_trace.h"
#include "peregrine/src/api/transport_types.h"
#include "peregrine/src/internal/base/types.h"
#include "peregrine/src/internal/chunk/header.h"
#include "peregrine/src/internal/trace/trace_context.h"
#include "peregrine/src/internal/trace/trace_event.h"

namespace peregrine::internal::testing {
namespace {

TEST(TraceRecorderTest, UntracedFastPathShortCircuits) {
  TraceRecorder recorder(TraceConfig{.sample_every_n = 0});
  const TraceContext ctx = recorder.MaybeStartTrace();
  EXPECT_FALSE(ctx.is_traced);

  recorder.RecordEngineEnqueued(ctx, Handle(1), TraceRecorder::NowUs(), 1,
                                Op::kWrite);
  DataHeader chunk = {};
  chunk.opcode = Opcode::kData;
  chunk.handle = Handle(1);
  chunk.len = 1024;
  chunk.reqid = ReqId(10);
  chunk.index = chunk_t(0);
  chunk.raddr = addr_t(0x1000);
  recorder.RecordWorkerEnqueued(ctx, chunk, 1, Op::kWrite);
  EXPECT_FALSE(chunk.trace.IsTraced());
  EXPECT_EQ(recorder.Buffer().TotalRecorded(), 0);
}

TEST(TraceRecorderTest, ConfigureRuntimeAndClear) {
  TraceRecorder recorder(TraceConfig{.sample_every_n = 0});
  EXPECT_FALSE(recorder.Sampler().IsEnabled());

  recorder.Configure(TraceConfig{.sample_every_n = 1});
  EXPECT_TRUE(recorder.Sampler().IsEnabled());
  const TraceContext ctx = recorder.MaybeStartTrace();
  EXPECT_TRUE(ctx.is_traced);
  recorder.RecordEngineEnqueued(ctx, Handle(1), TraceRecorder::NowUs(), 1,
                                Op::kWrite);
  EXPECT_EQ(recorder.Buffer().TotalRecorded(), 2);

  // Clear buffer without changing config.
  recorder.Clear();
  EXPECT_TRUE(recorder.Sampler().IsEnabled());
  EXPECT_EQ(recorder.Buffer().TotalRecorded(), 0);

  // Disable tracing.
  recorder.Configure(TraceConfig{.sample_every_n = 0});
  EXPECT_FALSE(recorder.Sampler().IsEnabled());
}

TEST(TraceRecorderTest, RecordAllLifecycleStages) {
  TraceRecorder recorder(TraceConfig{.sample_every_n = 1});
  const TraceContext ctx = recorder.MaybeStartTrace();
  ASSERT_TRUE(ctx.is_traced);

  constexpr Handle kHandle(42);
  constexpr ReqId kReqId(100);
  Byte buf[4096] = {};
  const Request req = {
      .op = Op::kWrite,
      .laddr = buf,
      .raddr = buf,
      .len = sizeof(buf),
  };

  recorder.RecordEngineEnqueued(ctx, kHandle, TraceRecorder::NowUs(),
                                /*num_requests=*/2, Op::kWrite);
  recorder.RecordEngineDequeued(ctx, kHandle, kReqId, req);

  recorder.RecordConnSetupStart(ctx, kHandle, /*num_conns=*/4);
  recorder.RecordGetPeerHostInfoDone(ctx, kHandle);
  recorder.RecordConnSetupDone(ctx, kHandle, /*num_workers=*/4);

  DataHeader chunk = {};
  chunk.opcode = Opcode::kData;
  chunk.handle = kHandle;
  chunk.len = 4096;
  chunk.reqid = kReqId;
  chunk.index = chunk_t(0);
  chunk.raddr = addr_t(0x2000);
  recorder.RecordWorkerEnqueued(ctx, chunk, /*send_worker_id=*/2, Op::kWrite);
  EXPECT_TRUE(chunk.trace.IsTraced());

  recorder.RecordWorkerDequeued(chunk, /*send_worker_id=*/2);
  recorder.RecordTxWriteDone(chunk, /*send_worker_id=*/2);

  chunk.trace.rx_worker_id = -3;
  chunk.trace.rx_chunk_status = 0;
  chunk.trace.rx_acquire_us = 12;
  chunk.trace.rx_payload_us = 150;
  recorder.RecordRxChunkDone(chunk, TraceRecorder::NowUs());
  recorder.RecordTxAckRcvd(chunk);

  recorder.RecordPollInProgress(ctx, kHandle, TraceRecorder::NowUs());
  recorder.RecordAllChunksDone(ctx, kHandle, kReqId, chunk_t(0),
                               /*worker_id=*/2);
  recorder.RecordCallbackDone(ctx, kHandle, /*worker_id=*/2);
  recorder.RecordPollReaped(ctx, kHandle, TraceRecorder::NowUs());

  const std::vector<TraceEvent> events = recorder.Buffer().Snapshot();
  ASSERT_EQ(events.size(), 15);

  EXPECT_EQ(events[0].stage, TraceStage::kPostEntry);
  EXPECT_EQ(events[0].handle, kHandle.value());
  EXPECT_EQ(events[0].chunk_idx, TraceEvent::kNoChunk);
  EXPECT_EQ(events[0].metric1, 2);

  EXPECT_EQ(events[1].stage, TraceStage::kEngineEnqueued);
  EXPECT_EQ(events[1].metric1, 2);

  EXPECT_EQ(events[2].stage, TraceStage::kEngineDequeued);
  EXPECT_EQ(events[2].reqid, kReqId.value());
  EXPECT_EQ(events[2].metric1, 4096);

  EXPECT_EQ(events[3].stage, TraceStage::kConnSetupStart);
  EXPECT_EQ(events[3].metric1, 4);
  EXPECT_EQ(events[4].stage, TraceStage::kGetPeerHostInfoDone);
  EXPECT_EQ(events[5].stage, TraceStage::kConnSetupDone);
  EXPECT_EQ(events[5].metric1, 4);

  EXPECT_EQ(events[6].stage, TraceStage::kWorkerEnqueued);
  EXPECT_EQ(events[6].chunk_idx, 0);
  EXPECT_EQ(events[6].worker_id, 2);

  EXPECT_EQ(events[7].stage, TraceStage::kWorkerDequeued);
  EXPECT_EQ(events[7].worker_id, 2);

  EXPECT_EQ(events[8].stage, TraceStage::kTxWriteDone);

  EXPECT_EQ(events[9].stage, TraceStage::kRxChunkDone);
  EXPECT_EQ(events[9].metric1, 150);
  EXPECT_EQ(events[9].metric2, 12);
  EXPECT_EQ(events[9].worker_id, -3);

  EXPECT_EQ(events[10].stage, TraceStage::kTxAckRcvd);
  EXPECT_EQ(events[10].metric1, 150);
  EXPECT_EQ(events[10].metric2, 12);
  EXPECT_EQ(events[10].worker_id, -3);

  EXPECT_EQ(events[11].stage, TraceStage::kPollInProgress);
  EXPECT_GE(events[11].metric1, 1);

  EXPECT_EQ(events[12].stage, TraceStage::kAllChunksDone);
  EXPECT_EQ(events[12].worker_id, 2);

  EXPECT_EQ(events[13].stage, TraceStage::kCallbackDone);
  EXPECT_EQ(events[14].stage, TraceStage::kPollReaped);
}

}  // namespace
}  // namespace peregrine::internal::testing
