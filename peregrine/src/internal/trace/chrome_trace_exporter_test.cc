#include "peregrine/src/internal/trace/chrome_trace_exporter.h"

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "absl/strings/match.h"
#include "absl/strings/str_split.h"
#include "peregrine/src/api/transport_types.h"
#include "peregrine/src/internal/base/types.h"
#include "peregrine/src/internal/trace/trace_event.h"

namespace peregrine::internal::testing {
namespace {

using ::testing::AllOf;
using ::testing::HasSubstr;
using ::testing::Not;
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

// Returns all lines in json that contain needle.
std::vector<std::string_view> FindLines(std::string_view json,
                                        std::string_view needle) {
  std::vector<std::string_view> matches;
  for (const std::string_view line : absl::StrSplit(json, '\n')) {
    if (absl::StrContains(line, needle)) matches.push_back(line);
  }
  return matches;
}

// Returns the single event line containing needle, or empty if not unique.
std::string_view FindSingleLine(std::string_view json,
                                std::string_view needle) {
  const std::vector<std::string_view> matches = FindLines(json, needle);
  EXPECT_THAT(matches, SizeIs(1)) << "needle: " << needle;
  return matches.size() == 1 ? matches[0] : "";
}

// The Record*() methods append events encoded like the TraceRecorder method of
// the same name, but with explicit timestamps.
class ChromeTraceExporterTest : public ::testing::Test {
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

  void RecordRequestDone(const TracedChunk& c, uint64_t t_us) {
    RecordCompletionStage(c, TraceStage::kRequestDone, t_us);
  }

  void RecordAllRequestsDone(const TracedChunk& c, uint64_t t_us) {
    RecordCompletionStage(c, TraceStage::kAllRequestsDone, t_us);
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
    RecordRequestDone(kChunk1, 1751);
    RecordTxWriteDone(kChunk2, 1880);
    RecordTxAckRcvd(kChunk2, 1950, {.payload_us = 190, .acquire_us = 4});
    RecordRequestDone(kChunk2, 1955);
    RecordAllRequestsDone(kChunk2, 1955);
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
    RecordRequestDone(chunk, t_us + 71);
    RecordAllRequestsDone(chunk, t_us + 71);
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

  void RecordCompletionStage(const TracedChunk& c, TraceStage stage,
                             uint64_t t_us) {
    events_.push_back({.timestamp_us = t_us,
                       .handle = c.handle,
                       .reqid = c.reqid,
                       .stage = stage,
                       .worker_id = c.send_worker_id,
                       .chunk_idx = c.index});
  }
};

TEST_F(ChromeTraceExporterTest, ColdStartPolledPost) {
  RecordColdStartPolledPost();

  const std::string json =
      ChromeTraceExporter::Export(events_, "10.0.0.1:5000");

  EXPECT_THAT(json, HasSubstr("[10.0.0.1:5000]"));
  EXPECT_THAT(json, HasSubstr(R"("tid":1)"));
  EXPECT_THAT(json, HasSubstr(R"("tid":2)"));
  EXPECT_THAT(json, HasSubstr(R"("tid":101)"));
  EXPECT_THAT(json, HasSubstr(R"("tid":201)"));
  EXPECT_THAT(json, HasSubstr(R"("tid":301)"));

  EXPECT_THAT(FindSingleLine(
                  json, "TransportImpl::Post(handle=0x1001, 2 reqs, 8192 B)"),
              HasSubstr(R"("enqueue_overhead_us":18)"));
  EXPECT_THAT(FindSingleLine(json, "Engine::QueueWait(handle=0x1001)"),
              HasSubstr(R"("engine_queue_wait_us":2)"));
  EXPECT_THAT(FindSingleLine(json, "Engine::createSendWorkers [Cold-Start]"),
              HasSubstr(R"("conn_setup_us":430)"));
  EXPECT_THAT(FindSingleLine(json, "Control::GetPeerHostInfo (gRPC)"),
              HasSubstr(R"("dur":180)"));
  EXPECT_THAT(FindSingleLine(json, "Channel::Connect (1 channels)"),
              HasSubstr(R"("dur":250)"));

  EXPECT_THAT(
      FindSingleLine(json,
                     "Engine::processWrite(reqid=0x2001, 4096 B -> 1 chunks)"),
      HasSubstr(R"("chunk_fanout_us":5)"));
  EXPECT_THAT(FindSingleLine(json, "WorkerQueue(req=0x2001, chunk=0)\""),
              HasSubstr(R"("worker_queue_wait_us":10)"));
  EXPECT_THAT(FindSingleLine(json, "SendChunk(req=0x2001, chunk=0, 4096 B)"),
              AllOf(HasSubstr(R"("worker_queue_wait_us":10)"),
                    HasSubstr(R"("tx_write_us":220)")));
  EXPECT_THAT(FindSingleLine(json, "RecvChunk(req=0x2001, chunk=0, 4096 B)"),
              AllOf(HasSubstr(R"("rx_acquire_wait_us":5)"),
                    HasSubstr(R"("rx_payload_write_us":200)"),
                    HasSubstr(R"("rx_chunk_status":"kEmpty")")));
  EXPECT_THAT(json, HasSubstr("rx_acquire_to_read_c0"));
  EXPECT_THAT(FindSingleLine(json, "RecvAck & Update(req=0x2001, chunk=0)"),
              HasSubstr(R"("chunk_rtt_us":280)"));
  EXPECT_THAT(FindSingleLine(json, "kRequestDone (req=0x2001)"),
              HasSubstr(R"("request_e2e_us":751)"));

  // Second request includes fan-out of kReqId1 and HoL wait behind its send.
  EXPECT_THAT(
      FindSingleLine(json,
                     "Engine::processWrite(reqid=0x2002, 4096 B -> 1 chunks)"),
      HasSubstr(R"("chunk_fanout_us":9)"));
  EXPECT_THAT(
      FindSingleLine(json, "WorkerQueue(req=0x2002, chunk=0) [HoL Wait]"),
      HasSubstr(R"("worker_queue_wait_us":235)"));
  EXPECT_THAT(FindSingleLine(json, "SendChunk(req=0x2002, chunk=0, 4096 B)"),
              AllOf(HasSubstr(R"("worker_queue_wait_us":235)"),
                    HasSubstr(R"("tx_write_us":180)")));
  EXPECT_THAT(FindSingleLine(json, "RecvAck & Update(req=0x2002, chunk=0)"),
              HasSubstr(R"("chunk_rtt_us":250)"));
  EXPECT_THAT(FindSingleLine(json, "kRequestDone (req=0x2002)"),
              HasSubstr(R"("request_e2e_us":955)"));

  EXPECT_THAT(FindSingleLine(json, "kAllRequestsDone (handle=0x1001)"),
              HasSubstr(R"("transport_e2e_us":955)"));
  EXPECT_THAT(FindSingleLine(json, "PollWaitDelay (Ready before Poll)"),
              HasSubstr(R"("completion_pickup_us":45)"));
  EXPECT_THAT(
      FindSingleLine(json,
                     "TransportImpl::Poll(0x1001) -> kSuccess [kPollReaped]"),
      AllOf(HasSubstr(R"("transport_e2e_us":955)"),
            HasSubstr(R"("completion_pickup_us":45)")));
}

TEST_F(ChromeTraceExporterTest, WarmCallbackPost) {
  RecordWarmCallbackPost(kHandle, 100);

  const std::string json = ChromeTraceExporter::Export(events_);

  // Without connection setup, engine queue wait ends at T_EngineDequeued.
  EXPECT_THAT(FindSingleLine(json, "Engine::QueueWait(handle=0x1001)"),
              HasSubstr(R"("engine_queue_wait_us":20)"));
  EXPECT_THAT(json, Not(HasSubstr("Engine::createSendWorkers")));
  EXPECT_THAT(FindSingleLine(json, "kRequestDone (req=0x2001)"),
              HasSubstr(R"("request_e2e_us":71)"));
  EXPECT_THAT(FindSingleLine(json, "kAllRequestsDone (handle=0x1001)"),
              HasSubstr(R"("transport_e2e_us":71)"));
  EXPECT_THAT(
      FindSingleLine(json, "on_complete(Status::kSuccess) [kCallbackDone]"),
      AllOf(HasSubstr(R"("transport_e2e_us":71)"),
            HasSubstr(R"("completion_pickup_us":9)")));
  EXPECT_THAT(json, Not(HasSubstr("kPollReaped")));
}

TEST_F(ChromeTraceExporterTest, ReadPostHasNoChunks) {
  RecordEngineEnqueued(kHandle, 100, 110, /*num_requests=*/1, Op::kRead);
  RecordEngineDequeued(kHandle, kReqId1, 120, Op::kRead, /*len=*/64);
  RecordRequestDone({.reqid = kReqId1, .send_worker_id = 0}, 121);
  RecordAllRequestsDone({.reqid = kReqId1, .send_worker_id = 0}, 121);
  RecordPollReaped(kHandle, 130);

  const std::string json = ChromeTraceExporter::Export(events_);

  EXPECT_THAT(FindSingleLine(
                  json, "Engine::processRead(reqid=0x2001, 64 B -> 0 chunks)"),
              HasSubstr(R"("chunk_fanout_us":0)"));
  EXPECT_THAT(json, Not(HasSubstr("SendChunk(")));
  EXPECT_THAT(json, Not(HasSubstr("RecvChunk(")));
  EXPECT_THAT(FindSingleLine(json, "kRequestDone (req=0x2001)"),
              HasSubstr(R"("request_e2e_us":21)"));
  EXPECT_THAT(FindSingleLine(json, "kAllRequestsDone (handle=0x1001)"),
              HasSubstr(R"("transport_e2e_us":21)"));
  EXPECT_THAT(
      FindSingleLine(json,
                     "TransportImpl::Poll(0x1001) -> kSuccess [kPollReaped]"),
      HasSubstr(R"("completion_pickup_us":9)"));
}

TEST_F(ChromeTraceExporterTest, SortsPostsRequestsAndChunks) {
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

  const std::string json = ChromeTraceExporter::Export(events_);

  const auto pos_first_post = json.find("TransportImpl::Post(handle=0x1001");
  const auto pos_later_post = json.find("TransportImpl::Post(handle=0x1000");
  ASSERT_NE(pos_first_post, std::string::npos);
  ASSERT_NE(pos_later_post, std::string::npos);
  EXPECT_LT(pos_first_post, pos_later_post);

  const auto pos_req2 =
      json.find("Engine::processWrite(reqid=0x2002, 10 B -> 0 chunks)");
  const auto pos_req1 =
      json.find("Engine::processWrite(reqid=0x2001, 30 B -> 3 chunks)");
  ASSERT_NE(pos_req2, std::string::npos);
  ASSERT_NE(pos_req1, std::string::npos);
  EXPECT_LT(pos_req2, pos_req1);
  EXPECT_THAT(FindSingleLine(
                  json, "Engine::processWrite(reqid=0x2001, 30 B -> 3 chunks)"),
              HasSubstr(R"("chunk_fanout_us":11)"));

  const auto pos_c0 = json.find("SendChunk(req=0x2001, chunk=0, 10 B)");
  const auto pos_c1 = json.find("SendChunk(req=0x2001, chunk=1, 10 B)");
  const auto pos_c2 = json.find("SendChunk(req=0x2001, chunk=2, 10 B)");
  ASSERT_NE(pos_c0, std::string::npos);
  ASSERT_NE(pos_c1, std::string::npos);
  ASSERT_NE(pos_c2, std::string::npos);
  EXPECT_LT(pos_c0, pos_c1);
  EXPECT_LT(pos_c1, pos_c2);
  EXPECT_THAT(FindSingleLine(json, "SendChunk(req=0x2001, chunk=0, 10 B)"),
              HasSubstr(R"("tid":101)"));
  EXPECT_THAT(FindSingleLine(json, "SendChunk(req=0x2001, chunk=1, 10 B)"),
              HasSubstr(R"("tid":102)"));
  EXPECT_THAT(FindSingleLine(json, "SendChunk(req=0x2001, chunk=2, 10 B)"),
              HasSubstr(R"("tid":103)"));
}

TEST_F(ChromeTraceExporterTest, KeepsEarliestRepeatedStages) {
  RecordColdStartPolledPost();
  // A duplicate chunk of kReqId1 is ACKed again with kDone, which also
  // re-records kRequestDone and kAllRequestsDone before Poll() reaps the
  // handle.
  RecordTxAckRcvd(kChunk1, 1960,
                  {.payload_us = 1, .acquire_us = 1, .chunk_status = 2});
  RecordRequestDone(kChunk1, 1965);
  RecordAllRequestsDone(kChunk1, 1965);

  const std::string json = ChromeTraceExporter::Export(events_);

  EXPECT_THAT(FindSingleLine(json, "RecvChunk(req=0x2001, chunk=0, 4096 B)"),
              AllOf(HasSubstr(R"("rx_payload_write_us":200)"),
                    HasSubstr(R"("rx_chunk_status":"kEmpty")")));
  EXPECT_THAT(FindSingleLine(json, "RecvAck & Update(req=0x2001, chunk=0)"),
              HasSubstr(R"("chunk_rtt_us":280)"));
  EXPECT_THAT(FindSingleLine(json, "kRequestDone (req=0x2001)"),
              HasSubstr(R"("request_e2e_us":751)"));
  EXPECT_THAT(FindSingleLine(json, "kAllRequestsDone (handle=0x1001)"),
              HasSubstr(R"("transport_e2e_us":955)"));
}

TEST_F(ChromeTraceExporterTest, UncapturedOrInvertedStagesHaveZeroDurations) {
  RecordEngineEnqueued(kHandle, 1000, 1000, /*num_requests=*/1, Op::kWrite);
  RecordEngineDequeued(kHandle, kReqId1, 1000, Op::kWrite, /*len=*/4096);
  RecordWorkerEnqueued(kChunk1, 1100);
  RecordWorkerDequeued(kChunk1, 1050);

  const std::string json = ChromeTraceExporter::Export(events_);

  EXPECT_THAT(FindSingleLine(
                  json, "TransportImpl::Post(handle=0x1001, 1 reqs, 4096 B)"),
              HasSubstr(R"("enqueue_overhead_us":0)"));
  EXPECT_THAT(json, Not(HasSubstr("Engine::QueueWait")));
  EXPECT_THAT(json, Not(HasSubstr("WorkerQueue(")));
  EXPECT_THAT(FindSingleLine(json, "SendChunk(req=0x2001, chunk=0, 4096 B)"),
              AllOf(HasSubstr(R"("worker_queue_wait_us":0)"),
                    HasSubstr(R"("tx_write_us":0)")));
  EXPECT_THAT(json, Not(HasSubstr("RecvChunk(")));
  EXPECT_THAT(json, Not(HasSubstr("RecvAck & Update(")));
  EXPECT_THAT(json, Not(HasSubstr("kRequestDone")));
  EXPECT_THAT(json, Not(HasSubstr("kAllRequestsDone")));
}

TEST_F(ChromeTraceExporterTest, IgnoresInvalidEvents) {
  RecordWarmCallbackPost(kHandle, 100);
  // A cleared slot, and events without a valid handle, ReqId, or chunk.
  events_.push_back({.handle = kHandle, .stage = TraceStage::kAllRequestsDone});
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

  const std::string json = ChromeTraceExporter::Export(events_);

  EXPECT_THAT(FindLines(json, "TransportImpl::Post("), SizeIs(1));
  EXPECT_THAT(FindLines(json, "Engine::process"), SizeIs(1));
  EXPECT_THAT(FindLines(json, "SendChunk("), SizeIs(1));
  EXPECT_THAT(FindSingleLine(json, "kAllRequestsDone (handle=0x1001)"),
              HasSubstr(R"("transport_e2e_us":71)"));
}

TEST_F(ChromeTraceExporterTest, SkipsPostsWithoutPostEntry) {
  // The ring buffer overwrote the earlier events of this Post().
  RecordWorkerDequeued(kChunk1, 1470);
  RecordTxWriteDone(kChunk1, 1690);
  RecordTxAckRcvd(kChunk1, 1750, {.payload_us = 200});
  RecordAllRequestsDone(kChunk1, 1755);

  EXPECT_THAT(ChromeTraceExporter::Export(events_),
              Not(HasSubstr("TransportImpl::Post(")));
}

TEST_F(ChromeTraceExporterTest, EmptySnapshot) {
  EXPECT_THAT(ChromeTraceExporter::Export({}),
              Not(HasSubstr("TransportImpl::Post(")));
}

}  // namespace
}  // namespace peregrine::internal::testing
