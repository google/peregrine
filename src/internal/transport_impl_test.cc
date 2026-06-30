#include <algorithm>
#include <cstddef>
#include <string>
#include <thread>  // NOLINT
#include <tuple>
#include <vector>

#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "absl/log/check.h"
#include "absl/log/log.h"
#include "absl/strings/str_format.h"
#include "absl/time/clock.h"
#include "absl/time/time.h"
#include "absl/types/span.h"
#include "src/api/transport.h"
#include "src/api/transport_types.h"
#include "src/internal/util/util.h"
#include "src/util/app.h"

namespace peregrine::internal::testing {
namespace {

using ::testing::Combine;
using ::testing::Eq;
using ::testing::Ne;
using ::testing::Pointwise;
using ::testing::TestParamInfo;
using ::testing::Values;

using Param = std::tuple</*buf_size=*/size_t, /*num_conns_per_peer=*/int>;

std::string ToString(const TestParamInfo<Param>& info) {
  const size_t size = std::get<0>(info.param);
  const int nconns = std::get<1>(info.param);
  return absl::StrFormat("BufSize_%zu_NumConnsPerPeer_%d", size, nconns);
}

class TransportImplTest : public ::testing::TestWithParam<Param> {
 protected:
  TransportImplTest()
      : size_(std::get<0>(GetParam())),
        first_(std::max(1UL, size_ / 2)),
        second_(size_ - first_),
        nconns_(std::get<1>(GetParam())),
        a_(size_, nconns_),
        b_(size_, nconns_) {
    DCHECK_EQ(a_.DataSize(), b_.DataSize());
  }

  std::vector<Request> MakeRequests(const Op op) {
    const auto pa = a_.DataPtr();
    const auto pb = b_.DataPtr();
    std::vector<Request> reqs;
    if (second_ < 1) {
      reqs.push_back({op, pa, pb, size_});
    } else {
      reqs.push_back({op, pa, pb, first_});
      reqs.push_back({op, pa + first_, pb + first_, second_});
    }
    return reqs;
  }

  static std::string Info(const Request& req, const Handle h, const Status s) {
    return absl::StrFormat(
        "TransportImplTest: %s handle = 0x%x, status = %s @ thread #%s",
        ToString(req.op), h.value(), ToString(s), ThreadId());
  }

  void WaitForCompletion(Transport& t, const Handle h,
                         absl::Span<const Request> reqs) {
    while (true) {
      ASSERT_OK_AND_ASSIGN(const Status s, t.Poll(h));
      for (const auto& req : reqs) {
        LOG(INFO) << Info(req, h, s);
      }
      if (IsCompleted(s)) break;
      absl::SleepFor(absl::Seconds(1));
    }
  }

 protected:
  const size_t size_;
  const size_t first_;
  const size_t second_;
  const int nconns_;
  util::App a_;
  util::App b_;
};

INSTANTIATE_TEST_SUITE_P(, TransportImplTest,
                         Combine(/*buf_size=*/Values(1, 65536, 1048575),
                                 /*num_conns_per_peer=*/Values(1, 8, 16)),
                         ToString);

TEST_P(TransportImplTest, Read) {
  // Pre-condition: no single byte at A is equal to B.
  a_.ClearData();
  b_.GenData();
  ASSERT_THAT(a_.Data(), Pointwise(Ne(), b_.Data()));

  // Use one thread to emulate a local process.
  std::thread a([this]() {
    Transport& t = a_.GetTransport();
    const std::string peer = b_.GetEndpoint();
    const Request req = {
        .op = Op::kRead,
        .laddr = a_.DataPtr(),
        .raddr = b_.DataPtr(),
        .len = a_.DataSize(),
    };
    const std::vector<Request> reqs = {req};
    ASSERT_OK_AND_ASSIGN(const Handle h, t.Post(peer, reqs));
    WaitForCompletion(t, h, reqs);
  });

  // Use another thread to emulate a remote process.
  std::thread b([]() {
    // TODO(yongx): nothing needed yet.
  });

  absl::SleepFor(absl::Seconds(1));
  a.join();
  b.join();

  // Post-condition: all the bytes at A are equal to B.
  EXPECT_THAT(a_.Data(), Pointwise(Eq(), b_.Data()));
}

TEST_P(TransportImplTest, Write) {
  // Pre-condition: no single byte at B is equal to A.
  a_.GenData();
  b_.ClearData();
  ASSERT_THAT(b_.Data(), Pointwise(Ne(), a_.Data()));

  // Use one thread to emulate a local process.
  std::thread a([this]() {
    Transport& t = a_.GetTransport();
    const std::string peer = b_.GetEndpoint();
    const std::vector<Request> reqs = MakeRequests(Op::kWrite);
    ASSERT_OK_AND_ASSIGN(const Handle h, t.Post(peer, reqs));
    WaitForCompletion(t, h, reqs);
  });

  // Use another thread to emulate a remote process.
  std::thread b([]() {
    // No code is needed.
  });

  a.join();
  b.join();

  // Post-condition: all the bytes at B are equal to A.
  EXPECT_THAT(b_.Data(), Pointwise(Eq(), a_.Data()));
}

}  // namespace
}  // namespace peregrine::internal::testing
