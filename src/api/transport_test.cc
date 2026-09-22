#include "src/api/transport.h"

#include <array>
#include <cstddef>
#include <cstring>
#include <string>
#include <tuple>
#include <utility>

#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "absl/functional/any_invocable.h"
#include "absl/log/check.h"
#include "absl/status/statusor.h"
#include "absl/strings/str_format.h"
#include "absl/synchronization/notification.h"
#include "absl/time/clock.h"
#include "absl/time/time.h"
#include "src/api/transport_metrics.h"
#include "src/api/transport_types.h"
#include "src/util/app.h"

namespace peregrine::testing {
namespace {

using ::testing::Eq;
using ::testing::Ne;
using ::testing::Pointwise;
using ::testing::TestParamInfo;
using ::testing::Values;

using Param = std::tuple</*completion_callback=*/bool>;

std::string ToString(const TestParamInfo<Param>& info) {
  const bool completion_callback = std::get<0>(info.param);
  return absl::StrFormat("CompletionCallback_%s",
                         completion_callback ? "true" : "false");
}

class TransportTest : public ::testing::TestWithParam<Param> {
  static constexpr size_t kBufSize = (64UL << 20) - 1;
  static constexpr int kNumConnsPerPeer = 8;

 protected:
  TransportTest()
      : completion_callback_(std::get<0>(GetParam())),
        completed_status_(Status::kNotFound),
        partial_(kBufSize / 2),
        l_(kBufSize, kNumConnsPerPeer),
        r_(kBufSize, kNumConnsPerPeer) {
    CHECK(1 <= partial_ && partial_ < l_.DataSize());
    DCHECK_EQ(l_.DataSize(), r_.DataSize());
  }

  absl::AnyInvocable<void(Status)> GetCompletionCallback() {
    if (!completion_callback_) return nullptr;
    return [this](Status s) {
      completed_status_ = s;
      done_.Notify();
    };
  }

  Status WaitForCompletion(Transport& t, const Handle h) {
    if (completion_callback_) {
      done_.WaitForNotificationWithTimeout(absl::Seconds(10));
      return completed_status_;
    }
    while (true) {
      const absl::StatusOr<Status> s = t.Poll(h);
      CHECK_OK(s) << s.status();
      if (IsCompleted(*s)) {
        completed_status_ = *s;
        return completed_status_;
      }
      absl::SleepFor(absl::Milliseconds(100));
    }
  }

  bool CheckMetrics(const TransportMetrics& metrics) {
    return metrics.requests_posted > 0 && metrics.e2e_write_errors == 0 &&
           metrics.e2e_write_latency_us.Count() > 0;
  }

 protected:
  const bool completion_callback_;
  Status completed_status_;
  absl::Notification done_;
  const size_t partial_;
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
  ASSERT_THAT(completed_status_, Ne(Status::kSuccess));

  // Post a read request (local <- remote).
  Transport& lt = l_.GetTransport();
  const std::string peer = r_.GetControlEndpoint();
  const Request req = {
      .op = Op::kRead,
      .laddr = l_.DataPtr(),
      .raddr = r_.DataPtr(),
      .len = l_.DataSize(),
  };
  auto callback = GetCompletionCallback();
  const absl::StatusOr<Handle> h = lt.Post(peer, {req}, std::move(callback));
  ASSERT_TRUE(h.ok()) << h.status();

  // Wait for the transport to finish processing the request.
  ASSERT_THAT(WaitForCompletion(lt, *h), Eq(Status::kSuccess));

  // Post-condition: all the local bytes are equal to the remote.
  EXPECT_THAT(l_.Data(), Pointwise(Eq(), r_.Data()));
}

TEST_P(TransportTest, Write) {
  // Pre-condition: no single remote byte is equal to the local.
  l_.GenData();
  r_.ClearData();
  ASSERT_THAT(r_.Data(), Pointwise(Ne(), l_.Data()));
  ASSERT_THAT(completed_status_, Ne(Status::kSuccess));

  // Post multiple write requests (local -> remote).
  Transport& lt = l_.GetTransport();
  const std::string peer = r_.GetControlEndpoint();
  const Request req1 = {
      .op = Op::kWrite,
      .laddr = l_.DataPtr(),
      .raddr = r_.DataPtr(),
      .len = partial_,
  };
  const Request req2 = {
      .op = Op::kWrite,
      .laddr = l_.DataPtr() + partial_,
      .raddr = r_.DataPtr() + partial_,
      .len = l_.DataSize() - partial_,
  };
  std::array<Request, 2> reqs = {req1, req2};
  auto callback = GetCompletionCallback();
  const absl::StatusOr<Handle> h = lt.Post(peer, reqs, std::move(callback));
  ASSERT_TRUE(h.ok()) << h.status();

  // Wait for the transport to finish processing the requests.
  ASSERT_THAT(WaitForCompletion(lt, *h), Eq(Status::kSuccess));

  // Post-condition: all the remote bytes are equal to the local.
  EXPECT_THAT(r_.Data(), Pointwise(Eq(), l_.Data()));
  EXPECT_TRUE(CheckMetrics(lt.GetTransportMetrics()));
}

}  // namespace
}  // namespace peregrine::testing
