#include "peregrine/src/api/transport.h"

#include <array>
#include <cstddef>
#include <cstring>
#include <string>
#include <tuple>
#include <utility>
#include <vector>

#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "absl/base/thread_annotations.h"
#include "absl/functional/any_invocable.h"
#include "absl/log/check.h"
#include "absl/status/statusor.h"
#include "absl/strings/str_format.h"
#include "absl/synchronization/mutex.h"
#include "absl/synchronization/notification.h"
#include "absl/time/clock.h"
#include "absl/time/time.h"
#include "absl/types/span.h"
#include "peregrine/src/api/transport_metrics.h"
#include "peregrine/src/api/transport_trace.h"
#include "peregrine/src/api/transport_types.h"
#include "peregrine/src/util/app.h"

namespace peregrine::testing {
namespace {

using ::testing::Eq;
using ::testing::HasSubstr;
using ::testing::Ne;
using ::testing::Not;
using ::testing::Pointwise;
using ::testing::TestParamInfo;
using ::testing::UnorderedElementsAre;
using ::testing::Values;

using Param = std::tuple</*completion_callback=*/bool>;

std::string ToString(const TestParamInfo<Param>& info) {
  const bool completion_callback = std::get<0>(info.param);
  return absl::StrFormat("%sCompletionCallback",
                         completion_callback ? "With" : "No");
}

class TransportTest : public ::testing::TestWithParam<Param> {
  static constexpr size_t kBufSize = (16UL << 20) - 1;
  static constexpr int kNumConnsPerPeer = 4;

 protected:
  struct PostEvent {
    Status status;
    Metadata post_meta;
    std::vector<Metadata> req_metas;
  };

  TransportTest()
      : completion_callback_(std::get<0>(GetParam())),
        completed_status_(Status::kNotFound),
        partial_(kBufSize / 2),
        l_(kBufSize, kNumConnsPerPeer),
        r_(
            kBufSize, kNumConnsPerPeer,
            [this](Status s, const Metadata& post_meta,
                   absl::Span<const Metadata> req_metas) {
              absl::MutexLock lock(cb_mu_);
              recv_posts_.push_back(PostEvent{
                  .status = s,
                  .post_meta = post_meta,
                  .req_metas =
                      std::vector<Metadata>(req_metas.begin(), req_metas.end()),
              });
            },
            [this](const Metadata& post_meta,
                   const Metadata& req_meta) -> Byte* {
              if (post_meta != Metadata{7, 8, 9}) return nullptr;
              if (req_meta == Metadata{1, 2, 3}) return r_.DataPtr();
              if (req_meta == Metadata{4, 5, 6}) return r_.DataPtr() + partial_;
              return nullptr;
            }) {
    CHECK(1 <= partial_ && partial_ < l_.DataSize());
    DCHECK_EQ(l_.DataSize(), r_.DataSize());
    l_.GetTransport().ConfigureTrace(TraceConfig{.sample_every_n = 1});
  }

  absl::AnyInvocable<void(Status)> GetCompletionCallback() {
    if (completion_callback_) {
      return [this](Status s) {
        completed_status_ = s;
        done_.Notify();
      };
    } else {
      return nullptr;
    }
  }

  void WaitForCompletion(Transport& t, const Handle h) {
    const absl::Duration timeout = absl::Seconds(10);
    if (completion_callback_) {
      done_.WaitForNotificationWithTimeout(timeout);
    } else {
      const absl::Time deadline = absl::Now() + timeout;
      while (absl::Now() < deadline) {
        const absl::StatusOr<Status> s = t.Poll(h);
        CHECK_OK(s) << s.status();
        if (IsCompleted(*s)) {
          completed_status_ = *s;
          return;
        }
        absl::SleepFor(absl::Milliseconds(100));
      }
    }
  }

  bool CheckMetrics(const OpMetrics& metrics) {
    return metrics.request_size_bytes.Count() > 0 && metrics.errors == 0 &&
           metrics.e2e_latency_us.Count() > 0;
  }

 protected:
  const bool completion_callback_;
  Status completed_status_;
  absl::Notification done_;
  const size_t partial_;
  absl::Mutex cb_mu_;
  std::vector<PostEvent> recv_posts_ ABSL_GUARDED_BY(cb_mu_);
  util::App l_;  // local
  util::App r_;  // remote
};

INSTANTIATE_TEST_SUITE_P(, TransportTest,
                         /*completion_callback=*/Values(true, false), ToString);

TEST_P(TransportTest, Read) {
  // Pre-condition: no single local byte is equal to the remote.
  l_.ClearData();
  r_.GenData();
  ASSERT_THAT(l_.Data(), Pointwise(Ne(), r_.Data()));
  ASSERT_NE(completed_status_, Status::kSuccess);

  // Post multiple read requests (local <- remote).
  Transport& lt = l_.GetTransport();
  const Transport& rt = r_.GetTransport();
  const std::string peer = rt.GetControlEndpoint();
  const Request req1 = {
      .op = Op::kRead,
      .laddr = l_.DataPtr(),
      .raddr = r_.DataPtr(),
      .len = partial_,
      .metadata = {1, 2, 3},
  };
  const Request req2 = {
      .op = Op::kRead,
      .laddr = l_.DataPtr() + partial_,
      .raddr = nullptr,
      .len = l_.DataSize() - partial_,
      .metadata = {4, 5, 6},
  };
  std::array<Request, 2> reqs = {req1, req2};
  auto callback = GetCompletionCallback();
  const Metadata post_metadata = {7, 8, 9};
  const absl::StatusOr<Handle> h =
      lt.Post(peer, reqs, std::move(callback), post_metadata);
  ASSERT_TRUE(h.ok()) << h.status();

  // Wait for the transport to finish processing the requests.
  WaitForCompletion(lt, *h);
  ASSERT_EQ(completed_status_, Status::kSuccess);

  // Post-condition: all the local bytes are equal to the remote, and responder
  // post callback fired with the expected status and metadata.
  EXPECT_THAT(l_.Data(), Pointwise(Eq(), r_.Data()));
  EXPECT_TRUE(CheckMetrics(lt.GetTransportMetrics().read));
  EXPECT_EQ(lt.GetTransportMetrics().recv_post_latency_us.Count(), 0);
  EXPECT_EQ(rt.GetTransportMetrics().recv_post_latency_us.Count(), 0);
  {
    absl::MutexLock lock(cb_mu_);
    ASSERT_EQ(recv_posts_.size(), 1);
    EXPECT_EQ(recv_posts_[0].status, Status::kSuccess);
    EXPECT_EQ(recv_posts_[0].post_meta, post_metadata);
    EXPECT_THAT(recv_posts_[0].req_metas,
                UnorderedElementsAre(req1.metadata, req2.metadata));
  }
}

TEST_P(TransportTest, Write) {
  // Pre-condition: no single remote byte is equal to the local.
  l_.GenData();
  r_.ClearData();
  ASSERT_THAT(r_.Data(), Pointwise(Ne(), l_.Data()));
  ASSERT_NE(completed_status_, Status::kSuccess);

  // Post multiple write requests (local -> remote).
  Transport& lt = l_.GetTransport();
  const Transport& rt = r_.GetTransport();
  const std::string peer = rt.GetControlEndpoint();
  const Request req1 = {
      .op = Op::kWrite,
      .laddr = l_.DataPtr(),
      .raddr = r_.DataPtr(),
      .len = partial_,
      .metadata = {1, 2, 3},
  };
  const Request req2 = {
      .op = Op::kWrite,
      .laddr = l_.DataPtr() + partial_,
      .raddr = nullptr,
      .len = l_.DataSize() - partial_,
      .metadata = {4, 5, 6},
  };
  std::array<Request, 2> reqs = {req1, req2};
  auto callback = GetCompletionCallback();
  const Metadata post_metadata = {7, 8, 9};
  const absl::StatusOr<Handle> h =
      lt.Post(peer, reqs, std::move(callback), post_metadata);
  ASSERT_TRUE(h.ok()) << h.status();

  // Wait for the transport to finish processing the requests.
  WaitForCompletion(lt, *h);
  ASSERT_EQ(completed_status_, Status::kSuccess);

  // Post-condition: all the remote bytes are equal to the local, and receiver
  // post callback fired with the expected status and metadata.
  EXPECT_THAT(r_.Data(), Pointwise(Eq(), l_.Data()));
  EXPECT_TRUE(CheckMetrics(lt.GetTransportMetrics().write));
  EXPECT_EQ(lt.GetTransportMetrics().recv_post_latency_us.Count(), 0);
  EXPECT_EQ(rt.GetTransportMetrics().recv_post_latency_us.Count(), 1);
  {
    absl::MutexLock lock(cb_mu_);
    ASSERT_EQ(recv_posts_.size(), 1);
    EXPECT_EQ(recv_posts_[0].status, Status::kSuccess);
    EXPECT_EQ(recv_posts_[0].post_meta, post_metadata);
    EXPECT_THAT(recv_posts_[0].req_metas,
                UnorderedElementsAre(req1.metadata, req2.metadata));
  }

  // Verify end-to-end Chrome Trace JSON export.
  const std::string json = lt.ExportChromeTraceJson();
  EXPECT_THAT(json, HasSubstr("\"traceEvents\""));
  EXPECT_THAT(json, HasSubstr("TransportImpl::Post("));
  EXPECT_THAT(json, HasSubstr("Engine::createSendWorkers"));
  EXPECT_THAT(json, HasSubstr("SendChunk("));
  EXPECT_THAT(json, HasSubstr("RecvChunk("));
  EXPECT_THAT(json, HasSubstr("RecvAck & Update("));
  EXPECT_THAT(json, HasSubstr("kRequestDone (req="));
  EXPECT_THAT(json, HasSubstr("kAllRequestsDone"));

  lt.ClearTrace();
  EXPECT_THAT(lt.ExportChromeTraceJson(),
              Not(HasSubstr("TransportImpl::Post(")));
}

TEST_P(TransportTest, SmallWriteRequestsRoundRobinAcrossSendWorkers) {
  constexpr size_t kReqSize = 64UL << 10;
  constexpr size_t kNumReqs = 4;
  ASSERT_LE(kReqSize * kNumReqs, l_.DataSize());

  l_.GenData();
  r_.ClearData();

  Transport& lt = l_.GetTransport();
  const Transport& rt = r_.GetTransport();
  const std::string peer = rt.GetControlEndpoint();

  std::array<Request, kNumReqs> reqs;
  for (size_t i = 0; i < kNumReqs; ++i) {
    const size_t offset = i * kReqSize;
    reqs[i] = Request{
        .op = Op::kWrite,
        .laddr = l_.DataPtr() + offset,
        .raddr = r_.DataPtr() + offset,
        .len = kReqSize,
    };
  }
  auto callback = GetCompletionCallback();
  const absl::StatusOr<Handle> h = lt.Post(peer, reqs, std::move(callback));
  ASSERT_TRUE(h.ok()) << h.status();

  WaitForCompletion(lt, *h);
  ASSERT_EQ(completed_status_, Status::kSuccess);

  EXPECT_EQ(std::memcmp(r_.DataPtr(), l_.DataPtr(), kReqSize * kNumReqs), 0);

  const std::string json = lt.ExportChromeTraceJson();
  EXPECT_THAT(json, HasSubstr("65536 B -> 1 chunks)"));
  EXPECT_THAT(json, Not(HasSubstr("chunk=1")));
  EXPECT_THAT(json, HasSubstr(R"("tid":101)"));
  EXPECT_THAT(json, HasSubstr(R"("tid":102)"));
  EXPECT_THAT(json, HasSubstr(R"("tid":103)"));
  EXPECT_THAT(json, HasSubstr(R"("tid":104)"));
}

TEST_P(TransportTest, ConsecutiveSmallPostsRoundRobinAcrossSendWorkers) {
  constexpr size_t kSmallSize = 32UL << 10;
  constexpr int kNumConns = 4;
  ASSERT_LE(kSmallSize * kNumConns, l_.DataSize());

  l_.GenData();
  r_.ClearData();

  Transport& lt = l_.GetTransport();
  const Transport& rt = r_.GetTransport();
  const std::string peer = rt.GetControlEndpoint();

  auto post_and_wait = [&](absl::Span<const Request> reqs) {
    Status status = Status::kNotFound;
    absl::Notification done;
    absl::AnyInvocable<void(Status)> cb = nullptr;
    if (completion_callback_) {
      cb = [&](Status s) {
        status = s;
        done.Notify();
      };
    }
    const absl::StatusOr<Handle> h = lt.Post(peer, reqs, std::move(cb));
    ASSERT_TRUE(h.ok()) << h.status();
    const absl::Duration timeout = absl::Seconds(10);
    if (completion_callback_) {
      done.WaitForNotificationWithTimeout(timeout);
    } else {
      const absl::Time deadline = absl::Now() + timeout;
      while (absl::Now() < deadline) {
        const absl::StatusOr<Status> s = lt.Poll(*h);
        CHECK_OK(s) << s.status();
        if (IsCompleted(*s)) {
          status = *s;
          break;
        }
        absl::SleepFor(absl::Milliseconds(10));
      }
    }
    ASSERT_EQ(status, Status::kSuccess);
  };

  for (int i = 0; i < kNumConns; ++i) {
    const size_t offset = static_cast<size_t>(i) * kSmallSize;
    const std::array<Request, 1> req = {Request{
        .op = Op::kWrite,
        .laddr = l_.DataPtr() + offset,
        .raddr = r_.DataPtr() + offset,
        .len = kSmallSize,
    }};
    post_and_wait(req);
  }

  const std::string small_json = lt.ExportChromeTraceJson();
  EXPECT_THAT(small_json, HasSubstr("32768 B -> 1 chunks)"));
  EXPECT_THAT(small_json, Not(HasSubstr("chunk=1")));
  EXPECT_THAT(small_json, HasSubstr(R"("tid":101)"));
  EXPECT_THAT(small_json, HasSubstr(R"("tid":102)"));
  EXPECT_THAT(small_json, HasSubstr(R"("tid":103)"));
  EXPECT_THAT(small_json, HasSubstr(R"("tid":104)"));
  EXPECT_EQ(std::memcmp(r_.DataPtr(), l_.DataPtr(), kSmallSize * kNumConns), 0);
}

}  // namespace
}  // namespace peregrine::testing
