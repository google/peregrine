#include <cstddef>
#include <cstring>

#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "absl/log/check.h"
#include "absl/time/clock.h"
#include "absl/time/time.h"
#include "src/api/transport.h"
#include "src/api/types.h"
#include "test/integration/test_util.h"

namespace peregrine::integration_test {
namespace {

using ::testing::Each;
using ::testing::Eq;
using ::testing::Ne;
using ::testing::Pointwise;

constexpr Endpoint kPeer = "peer_hostname";
constexpr Byte kLocalByte = 0x05;
constexpr Byte kRemoteByte = 0x0e;
static_assert(kLocalByte != kRemoteByte);
static constexpr size_t kLen = 128 * 1024;

class SimpleTest : public testing::Test {
 protected:
  SimpleTest() : local_(kLen, kLocalByte), remote_(kLen, kRemoteByte) {
    CHECK_NE(kLocalByte, kRemoteByte);
  }

  Request MakeRequest(const Op op) {
    const auto l = local_.Data();
    const auto r = remote_.Data();
    DCHECK_EQ(l.size(), r.size());
    return Request{
        .op = op,
        .laddr = l.data(),
        .raddr = r.data(),
        .len = l.size(),
    };
  }

  void WaitForCompletion(Transport& t, const Handle h) {
    while (true) {
      ASSERT_OK_AND_ASSIGN(const Status s, t.Poll(h));
      if (IsCompleted(s)) {
        EXPECT_EQ(s, Status::kSuccess);
        break;
      }
      absl::SleepFor(absl::Milliseconds(10));
    }
  }

 protected:
  UserProcess local_;
  UserProcess remote_;
};

TEST_F(SimpleTest, Read) {
  // Pre-condition: no single local byte is equal to the remote.
  ASSERT_THAT(local_.Data(), Pointwise(Ne(), remote_.Data()));

  // Local: post a read request.
  Transport& lt = local_.GetTransport();
  const Request& req = MakeRequest(Op::kRead);
  ASSERT_OK_AND_ASSIGN(const Handle h, lt.Post(kPeer, req));

  // Local: wait for the transport to finish processing the request.
  WaitForCompletion(lt, h);

  // Post-condition: all the local bytes are equal to the remote.
  EXPECT_THAT(local_.Data(), Pointwise(Eq(), remote_.Data()));
  EXPECT_THAT(local_.Data(), Each(Eq(kRemoteByte)));
}

TEST_F(SimpleTest, Write) {
  // Pre-condition: no single remote byte is equal to the local.
  ASSERT_THAT(remote_.Data(), Pointwise(Ne(), local_.Data()));

  // Local: post a write request.
  Transport& lt = local_.GetTransport();
  const Request& req = MakeRequest(Op::kWrite);
  ASSERT_OK_AND_ASSIGN(const Handle h, lt.Post(kPeer, req));

  // Local: wait for the transport to finish processing the request.
  WaitForCompletion(lt, h);

  // Post-condition: all the remote bytes are equal to the local.
  EXPECT_THAT(remote_.Data(), Pointwise(Eq(), local_.Data()));
  EXPECT_THAT(remote_.Data(), Each(Eq(kLocalByte)));
}

}  // namespace
}  // namespace peregrine::integration_test
