#include <cstddef>
#include <cstring>
#include <string>

#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "absl/log/check.h"
#include "absl/time/clock.h"
#include "absl/time/time.h"
#include "src/api/transport.h"
#include "src/api/transport_types.h"
#include "src/util/app.h"

namespace peregrine::integration_test {
namespace {

using ::testing::Eq;
using ::testing::Ne;
using ::testing::Pointwise;

class SimpleTest : public testing::Test {
  static constexpr size_t kBufSize = (64UL << 20) - 1;
  static constexpr int kNumConnsPerPeer = 8;

 protected:
  SimpleTest()
      : partial_(kBufSize / 2),
        l_(kBufSize, kNumConnsPerPeer),
        r_(kBufSize, kNumConnsPerPeer) {
    CHECK(1 <= partial_ && partial_ < l_.DataSize());
    DCHECK_EQ(l_.DataSize(), r_.DataSize());
  }

  void WaitForCompletion(Transport& t, const Handle h) {
    while (true) {
      ASSERT_OK_AND_ASSIGN(const Status s, t.Poll(h));
      if (IsCompleted(s)) {
        CHECK_EQ(s, Status::kSuccess);
        break;
      }
      absl::SleepFor(absl::Milliseconds(100));
    }
  }

 protected:
  const size_t partial_;
  util::App l_;  // local
  util::App r_;  // remote
};

TEST_F(SimpleTest, Read) {
  // Pre-condition: no single local byte is equal to the remote.
  l_.ClearData();
  r_.GenData();
  ASSERT_THAT(l_.Data(), Pointwise(Ne(), r_.Data()));

  // Local: post a read request.
  Transport& lt = l_.GetTransport();
  const std::string peer = r_.GetEndpoint();
  const Request req = {
      .op = Op::kRead,
      .laddr = l_.DataPtr(),
      .raddr = r_.DataPtr(),
      .len = l_.DataSize(),
  };
  ASSERT_OK_AND_ASSIGN(const Handle h, lt.Post(peer, {req}));

  // Local: wait for the transport to finish processing the request.
  WaitForCompletion(lt, h);

  // Post-condition: all the local bytes are equal to the remote.
  EXPECT_THAT(l_.Data(), Pointwise(Eq(), r_.Data()));
}

TEST_F(SimpleTest, Write) {
  // Pre-condition: no single remote byte is equal to the local.
  l_.GenData();
  r_.ClearData();
  ASSERT_THAT(r_.Data(), Pointwise(Ne(), l_.Data()));

  // Local: post multiple write requests.
  Transport& lt = l_.GetTransport();
  const std::string peer = r_.GetEndpoint();
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
  ASSERT_OK_AND_ASSIGN(const Handle h, lt.Post(peer, {req1, req2}));

  // Local: wait for the transport to finish processing the requests.
  WaitForCompletion(lt, h);

  // Post-condition: all the remote bytes are equal to the local.
  EXPECT_THAT(r_.Data(), Pointwise(Eq(), l_.Data()));
}

}  // namespace
}  // namespace peregrine::integration_test
