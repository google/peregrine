#include "peregrine/src/internal/engine2/engine.h"

#include <array>
#include <cstddef>
#include <memory>
#include <utility>
#include <vector>

#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "absl/container/flat_hash_set.h"
#include "absl/status/status.h"
#include "absl/status/status_matchers.h"
#include "absl/status/statusor.h"
#include "absl/synchronization/notification.h"
#include "absl/time/clock.h"
#include "absl/time/time.h"
#include "absl/types/span.h"
#include "peregrine/src/api/transport_metrics.h"
#include "peregrine/src/api/transport_trace.h"
#include "peregrine/src/api/transport_types.h"
#include "peregrine/src/internal/base/config.h"
#include "peregrine/src/internal/base/endpoint.h"
#include "peregrine/src/internal/base/hostinfo.h"
#include "peregrine/src/internal/engine2/peer_discovery.h"
#include "peregrine/src/internal/engine2/peer_work_queue.h"

namespace peregrine::internal::engine2 {
namespace {

using ::absl_testing::IsOk;
using ::absl_testing::IsOkAndHolds;
using ::absl_testing::StatusIs;
using ::testing::IsEmpty;
using ::testing::Not;
using ::testing::NotNull;

GetPeerHostInfoFn StaticPeerLookup() {
  return [](const PeerId& peer) -> absl::StatusOr<HostInfo> {
    HostInfo info;
    info.control_plane_listener = peer;
    return info;
  };
}

class EngineTest : public ::testing::Test {
 protected:
  EngineTest() {
    self_.control_plane_listener = Endpoint::Create("127.0.0.1:10000");
  }

  std::unique_ptr<Engine> CreateEngine(
      GetPeerHostInfoFn lookup = StaticPeerLookup()) {
    return Engine::Create(Config{}, self_, std::move(lookup));
  }

  HostInfo self_;
  std::array<Byte, 64> buf_ = {};
};

TEST_F(EngineTest, ItemValidity) {
  const Request valid = {.op = Op::kWrite, .laddr = buf_.data(), .len = 64};
  const Request no_buffer = {.op = Op::kWrite, .laddr = nullptr, .len = 64};
  const Request empty_buffer = {.op = Op::kWrite, .laddr = buf_.data()};

  EXPECT_FALSE(Engine::Item{}.IsValid());

  const std::vector<Request> ok = {valid, valid};
  EXPECT_TRUE(Engine::Item{.requests = ok}.IsValid());

  const std::vector<Request> bad_addr = {valid, no_buffer};
  EXPECT_FALSE(Engine::Item{.requests = bad_addr}.IsValid());

  const std::vector<Request> bad_len = {empty_buffer};
  EXPECT_FALSE(Engine::Item{.requests = bad_len}.IsValid());
}

TEST_F(EngineTest, CreateRejectsInvalidConfigAndEncryption) {
  Config bad_conns;
  bad_conns.num_conns_per_peer = 0;
  EXPECT_EQ(Engine::Create(bad_conns, self_, StaticPeerLookup()), nullptr);

  Config encrypted;
  encrypted.require_dataplane_encryption = true;
  EXPECT_EQ(Engine::Create(encrypted, self_, StaticPeerLookup()), nullptr);

  Config rdma;
  rdma.transport_type = TransportType::kRdma;
  EXPECT_EQ(Engine::Create(rdma, self_, StaticPeerLookup()), nullptr);

  EXPECT_EQ(Engine::Create(Config{}, self_, /*get_peer_host_info_fn=*/nullptr),
            nullptr);
}

TEST_F(EngineTest, EnqueueRejectsReadsAndInvalidInputs) {
  const std::unique_ptr<Engine> engine = CreateEngine();
  ASSERT_THAT(engine, NotNull());

  const Endpoint peer = Endpoint::Create("127.0.0.1:20000");
  const std::vector<Request> read_req = {
      {.op = Op::kRead, .laddr = buf_.data(), .len = buf_.size()}};
  EXPECT_THAT(engine->Enqueue(peer, Engine::Item{.requests = read_req}),
              StatusIs(absl::StatusCode::kUnimplemented));

  const std::vector<Request> write_req = {
      {.op = Op::kWrite, .laddr = buf_.data(), .len = buf_.size()}};
  EXPECT_THAT(engine->Enqueue(Endpoint(), Engine::Item{.requests = write_req}),
              StatusIs(absl::StatusCode::kInvalidArgument));
  EXPECT_THAT(engine->Enqueue(peer, Engine::Item{}),
              StatusIs(absl::StatusCode::kInvalidArgument));
}

TEST_F(EngineTest, EnqueueReturnsUniqueHandlesAndTracksStatus) {
  // Keep lookup blocked until test ends so handles stay in progress.
  absl::Notification release;
  auto engine = CreateEngine([&](const PeerId&) -> absl::StatusOr<HostInfo> {
    release.WaitForNotification();
    return absl::UnavailableError("shutdown");
  });
  ASSERT_THAT(engine, NotNull());

  const std::vector<Request> reqs = {
      {.op = Op::kWrite, .laddr = buf_.data(), .len = buf_.size()}};
  const Endpoint peer = Endpoint::Create("127.0.0.1:20000");
  absl::flat_hash_set<Handle> handles;
  for (int i = 0; i < 10; ++i) {
    const absl::StatusOr<Handle> h =
        engine->Enqueue(peer, Engine::Item{.requests = reqs});
    ASSERT_THAT(h, IsOk());
    EXPECT_NE(*h, kInvalidHandle);
    EXPECT_TRUE(handles.insert(*h).second);
    EXPECT_THAT(engine->QueryUpdate(*h), IsOkAndHolds(Status::kInProgress));
  }
  EXPECT_THAT(engine->QueryUpdate(Handle(0)),
              StatusIs(absl::StatusCode::kNotFound));
  release.Notify();
}

TEST_F(EngineTest, SetupFailureInvokesOnCompleteWithFailure) {
  auto engine = CreateEngine([](const PeerId&) -> absl::StatusOr<HostInfo> {
    return absl::UnavailableError("unreachable");
  });
  ASSERT_THAT(engine, NotNull());

  absl::Notification done;
  Status final_status = Status::kInProgress;
  const std::vector<Request> reqs = {
      {.op = Op::kWrite, .laddr = buf_.data(), .len = buf_.size()}};
  const absl::StatusOr<Handle> h = engine->Enqueue(
      Endpoint::Create("127.0.0.1:20000"), Engine::Item{
                                               .requests = reqs,
                                               .on_complete =
                                                   [&](Status s) {
                                                     final_status = s;
                                                     done.Notify();
                                                   },
                                           });
  ASSERT_THAT(h, IsOk());
  ASSERT_TRUE(done.WaitForNotificationWithTimeout(absl::Seconds(10)));
  EXPECT_EQ(final_status, Status::kFailure);
}

TEST_F(EngineTest, SetupFailureReportedByQueryUpdateForPolledPost) {
  auto engine = CreateEngine([](const PeerId&) -> absl::StatusOr<HostInfo> {
    return absl::UnavailableError("unreachable");
  });
  ASSERT_THAT(engine, NotNull());

  const std::vector<Request> reqs = {
      {.op = Op::kWrite, .laddr = buf_.data(), .len = buf_.size()}};
  const absl::StatusOr<Handle> h = engine->Enqueue(
      Endpoint::Create("127.0.0.1:20001"), Engine::Item{.requests = reqs});
  ASSERT_THAT(h, IsOk());

  absl::StatusOr<Status> status = Status::kInProgress;
  const absl::Time deadline = absl::Now() + absl::Seconds(10);
  while (absl::Now() < deadline) {
    status = engine->QueryUpdate(*h);
    if (!status.ok() || *status != Status::kInProgress) break;
    absl::SleepFor(absl::Milliseconds(5));
  }
  EXPECT_THAT(status, IsOkAndHolds(Status::kFailure));
  // Reaped once observed by QueryUpdate.
  EXPECT_THAT(engine->QueryUpdate(*h), StatusIs(absl::StatusCode::kNotFound));
}

TEST_F(EngineTest, MemoryRegistrationAndMetrics) {
  const std::unique_ptr<Engine> engine = CreateEngine();
  ASSERT_THAT(engine, NotNull());

  EXPECT_THAT(engine->RegisterMemory(buf_.data(), buf_.size()), IsOk());
  EXPECT_THAT(engine->UnregisterMemory(buf_.data()), IsOk());

  TransportMetrics metrics;
  engine->GetMetrics(metrics);
  engine->ConfigureTrace(TraceConfig{.sample_every_n = 1});
  engine->ClearTrace();
  EXPECT_THAT(engine->ExportChromeTraceJson(), Not(IsEmpty()));
}

// Helper owning two loopback `Engine` instances and their `HostInfo`s.
struct LoopbackPair {
  HostInfo host_a;
  HostInfo host_b;
  std::unique_ptr<Engine> engine_a;
  std::unique_ptr<Engine> engine_b;

  void Init(int num_conns, RecvPostCallback recv_b = nullptr,
            ResolveAddrCallback resolve_b = nullptr) {
    Config cfg;
    cfg.num_conns_per_peer = num_conns;
    cfg.allowed_devices = {"lo"};
    host_a.control_plane_listener = Endpoint::Create("127.0.0.1:30001");
    host_b.control_plane_listener = Endpoint::Create("127.0.0.1:30002");

    auto lookup_a = [this](const PeerId& p) -> absl::StatusOr<HostInfo> {
      if (p == host_b.control_plane_listener) return host_b;
      return absl::NotFoundError("unknown peer");
    };
    auto lookup_b = [this](const PeerId& p) -> absl::StatusOr<HostInfo> {
      if (p == host_a.control_plane_listener) return host_a;
      return absl::NotFoundError("unknown peer");
    };
    engine_a = Engine::Create(cfg, host_a, std::move(lookup_a));
    engine_b = Engine::Create(cfg, host_b, std::move(lookup_b),
                              std::move(recv_b), std::move(resolve_b));
    ASSERT_THAT(engine_a, NotNull());
    ASSERT_THAT(engine_b, NotNull());
  }
};

TEST(EngineEndToEndTest, SingleWriteCallbackAndPolling) {
  LoopbackPair pair;
  pair.Init(/*num_conns=*/2);

  // 1. Callback completion.
  std::vector<Byte> src1(4096);
  for (size_t i = 0; i < src1.size(); ++i) src1[i] = static_cast<Byte>(i + 1);
  std::vector<Byte> dst1(src1.size(), 0);
  absl::Notification done1;
  Status status1 = Status::kInProgress;
  const std::vector<Request> reqs1 = {{.op = Op::kWrite,
                                       .laddr = src1.data(),
                                       .raddr = dst1.data(),
                                       .len = src1.size()}};
  ASSERT_THAT(pair.engine_a->Enqueue(pair.host_b.control_plane_listener,
                                     Engine::Item{
                                         .requests = reqs1,
                                         .on_complete =
                                             [&](Status s) {
                                               status1 = s;
                                               done1.Notify();
                                             },
                                     }),
              IsOk());
  ASSERT_TRUE(done1.WaitForNotificationWithTimeout(absl::Seconds(10)));
  EXPECT_EQ(status1, Status::kSuccess);
  EXPECT_EQ(dst1, src1);

  // 2. Polling completion via QueryUpdate.
  std::vector<Byte> src2(4096, 0xCC);
  std::vector<Byte> dst2(src2.size(), 0);
  const std::vector<Request> reqs2 = {{.op = Op::kWrite,
                                       .laddr = src2.data(),
                                       .raddr = dst2.data(),
                                       .len = src2.size()}};
  const absl::StatusOr<Handle> h2 = pair.engine_a->Enqueue(
      pair.host_b.control_plane_listener, Engine::Item{.requests = reqs2});
  ASSERT_THAT(h2, IsOk());

  absl::StatusOr<Status> polled = Status::kInProgress;
  const absl::Time deadline = absl::Now() + absl::Seconds(10);
  while (absl::Now() < deadline) {
    polled = pair.engine_a->QueryUpdate(*h2);
    if (!polled.ok() || *polled != Status::kInProgress) break;
    absl::SleepFor(absl::Milliseconds(2));
  }
  EXPECT_THAT(polled, IsOkAndHolds(Status::kSuccess));
  EXPECT_EQ(dst2, src2);
  EXPECT_THAT(pair.engine_a->QueryUpdate(*h2),
              StatusIs(absl::StatusCode::kNotFound));
}

TEST(EngineEndToEndTest, MultiRequestMultiChunkOverParallelConnections) {
  LoopbackPair pair;
  pair.Init(/*num_conns=*/4);

  const size_t len1 = kChunkSize * 3 + 123;
  const size_t len2 = kChunkSize * 2 + 456;
  std::vector<Byte> src1(len1);
  std::vector<Byte> src2(len2);
  for (size_t i = 0; i < len1; ++i) src1[i] = static_cast<Byte>((i * 7) & 0xff);
  for (size_t i = 0; i < len2; ++i)
    src2[i] = static_cast<Byte>((i * 13 + 5) & 0xff);
  std::vector<Byte> dst1(len1, 0);
  std::vector<Byte> dst2(len2, 0);

  absl::Notification done;
  Status final_status = Status::kInProgress;
  const std::vector<Request> reqs = {
      {.op = Op::kWrite,
       .laddr = src1.data(),
       .raddr = dst1.data(),
       .len = len1},
      {.op = Op::kWrite,
       .laddr = src2.data(),
       .raddr = dst2.data(),
       .len = len2},
  };
  ASSERT_THAT(pair.engine_a->Enqueue(pair.host_b.control_plane_listener,
                                     Engine::Item{
                                         .requests = reqs,
                                         .on_complete =
                                             [&](Status s) {
                                               final_status = s;
                                               done.Notify();
                                             },
                                     }),
              IsOk());
  ASSERT_TRUE(done.WaitForNotificationWithTimeout(absl::Seconds(10)));
  EXPECT_EQ(final_status, Status::kSuccess);
  EXPECT_EQ(dst1, src1);
  EXPECT_EQ(dst2, src2);
}

TEST(EngineEndToEndTest, BidirectionalConcurrentWrites) {
  LoopbackPair pair;
  pair.Init(/*num_conns=*/2);

  const size_t len = kChunkSize + 512;
  std::vector<Byte> src_ab(len, 0x11);
  std::vector<Byte> dst_ab(len, 0);
  std::vector<Byte> src_ba(len, 0x22);
  std::vector<Byte> dst_ba(len, 0);

  absl::Notification done_ab;
  absl::Notification done_ba;
  Status status_ab = Status::kInProgress;
  Status status_ba = Status::kInProgress;

  const std::vector<Request> req_ab = {{.op = Op::kWrite,
                                        .laddr = src_ab.data(),
                                        .raddr = dst_ab.data(),
                                        .len = len}};
  const std::vector<Request> req_ba = {{.op = Op::kWrite,
                                        .laddr = src_ba.data(),
                                        .raddr = dst_ba.data(),
                                        .len = len}};

  ASSERT_THAT(pair.engine_a->Enqueue(pair.host_b.control_plane_listener,
                                     Engine::Item{
                                         .requests = req_ab,
                                         .on_complete =
                                             [&](Status s) {
                                               status_ab = s;
                                               done_ab.Notify();
                                             },
                                     }),
              IsOk());
  ASSERT_THAT(pair.engine_b->Enqueue(pair.host_a.control_plane_listener,
                                     Engine::Item{
                                         .requests = req_ba,
                                         .on_complete =
                                             [&](Status s) {
                                               status_ba = s;
                                               done_ba.Notify();
                                             },
                                     }),
              IsOk());

  ASSERT_TRUE(done_ab.WaitForNotificationWithTimeout(absl::Seconds(10)));
  ASSERT_TRUE(done_ba.WaitForNotificationWithTimeout(absl::Seconds(10)));
  EXPECT_EQ(status_ab, Status::kSuccess);
  EXPECT_EQ(status_ba, Status::kSuccess);
  EXPECT_EQ(dst_ab, src_ab);
  EXPECT_EQ(dst_ba, src_ba);
}

TEST(EngineEndToEndTest, ResolveAddrAndRecvPostCallbacks) {
  std::vector<Byte> src(8192, 0x7E);
  std::vector<Byte> dst(src.size(), 0);
  Metadata expected_post_meta = {};
  expected_post_meta[0] = 0xAA;
  Metadata expected_req_meta = {};
  expected_req_meta[0] = 0xBB;

  absl::Notification recv_done;
  Status recv_status = Status::kInProgress;
  Metadata got_post_meta = {};
  Metadata got_req_meta = {};

  LoopbackPair pair;
  pair.Init(
      /*num_conns=*/2,
      /*recv_b=*/
      [&](Status s, const Metadata& post_meta,
          absl::Span<const Metadata> req_metas) {
        recv_status = s;
        got_post_meta = post_meta;
        if (!req_metas.empty()) got_req_meta = req_metas[0];
        recv_done.Notify();
      },
      /*resolve_b=*/
      [&](const Metadata&, const Metadata&) -> Byte* { return dst.data(); });

  absl::Notification send_done;
  Status send_status = Status::kInProgress;
  const std::vector<Request> reqs = {{.op = Op::kWrite,
                                      .laddr = src.data(),
                                      .raddr = nullptr,
                                      .len = src.size(),
                                      .metadata = expected_req_meta}};
  ASSERT_THAT(pair.engine_a->Enqueue(pair.host_b.control_plane_listener,
                                     Engine::Item{
                                         .requests = reqs,
                                         .on_complete =
                                             [&](Status s) {
                                               send_status = s;
                                               send_done.Notify();
                                             },
                                         .metadata = expected_post_meta,
                                     }),
              IsOk());

  ASSERT_TRUE(send_done.WaitForNotificationWithTimeout(absl::Seconds(10)));
  ASSERT_TRUE(recv_done.WaitForNotificationWithTimeout(absl::Seconds(10)));
  EXPECT_EQ(send_status, Status::kSuccess);
  EXPECT_EQ(recv_status, Status::kSuccess);
  EXPECT_EQ(got_post_meta, expected_post_meta);
  EXPECT_EQ(got_req_meta, expected_req_meta);
  EXPECT_EQ(dst, src);
}

}  // namespace
}  // namespace peregrine::internal::engine2
