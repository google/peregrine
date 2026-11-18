#include "peregrine/src/internal/engine2/engine.h"

#include <array>
#include <memory>
#include <vector>

#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "absl/functional/any_invocable.h"
#include "absl/status/status.h"
#include "absl/status/status_matchers.h"
#include "absl/status/statusor.h"
#include "peregrine/src/api/transport_metrics.h"
#include "peregrine/src/api/transport_trace.h"
#include "peregrine/src/api/transport_types.h"
#include "peregrine/src/internal/base/config.h"
#include "peregrine/src/internal/base/endpoint.h"
#include "peregrine/src/internal/base/hostinfo.h"

namespace peregrine::internal::engine2 {
namespace {

using ::absl_testing::IsOk;
using ::absl_testing::StatusIs;
using ::testing::IsEmpty;
using ::testing::Not;
using ::testing::NotNull;

// Peer lookup hook that fails every lookup; no test here connects to a peer.
Engine::GetPeerHostInfoFn FailingPeerLookup() {
  return [](const Endpoint&,
            absl::AnyInvocable<void(absl::StatusOr<HostInfo>)> on_done) {
    on_done(absl::UnavailableError("no peers in this test"));
  };
}

class EngineTest : public ::testing::Test {
 protected:
  EngineTest() {
    self_.control_plane_listener = Endpoint::Create("127.0.0.1:10000");
  }

  std::unique_ptr<Engine> CreateEngine() {
    return Engine::Create(Config{}, self_, FailingPeerLookup());
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

TEST_F(EngineTest, CreateRejectsInvalidConfig) {
  Config config;
  config.num_conns_per_peer = 0;
  EXPECT_EQ(Engine::Create(config, self_, FailingPeerLookup()), nullptr);
}

TEST_F(EngineTest, CreateWithoutControlPlane) {
  EXPECT_THAT(CreateEngine(), NotNull());
}

TEST_F(EngineTest, UnimplementedRequestPathReturnsUnimplemented) {
  const std::unique_ptr<Engine> engine = CreateEngine();
  ASSERT_THAT(engine, NotNull());

  const std::vector<Request> reqs = {
      {.op = Op::kWrite, .laddr = buf_.data(), .len = buf_.size()}};
  EXPECT_THAT(engine->Enqueue(Endpoint::Create("127.0.0.1:20000"),
                              Engine::Item{.requests = reqs}),
              StatusIs(absl::StatusCode::kUnimplemented));
  EXPECT_THAT(engine->QueryUpdate(Handle(1)),
              StatusIs(absl::StatusCode::kUnimplemented));
}

TEST_F(EngineTest, MemoryRegistrationSucceeds) {
  const std::unique_ptr<Engine> engine = CreateEngine();
  ASSERT_THAT(engine, NotNull());

  EXPECT_THAT(engine->RegisterMemory(buf_.data(), buf_.size()), IsOk());
  EXPECT_THAT(engine->UnregisterMemory(buf_.data()), IsOk());
}

TEST_F(EngineTest, MetricsAndTracing) {
  const std::unique_ptr<Engine> engine = CreateEngine();
  ASSERT_THAT(engine, NotNull());

  TransportMetrics metrics;
  engine->GetMetrics(metrics);

  engine->ConfigureTrace(TraceConfig{.sample_every_n = 1});
  engine->ClearTrace();
  EXPECT_THAT(engine->ExportChromeTraceJson(), Not(IsEmpty()));
}

}  // namespace
}  // namespace peregrine::internal::engine2
