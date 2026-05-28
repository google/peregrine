#include <cstddef>
#include <cstring>
#include <string_view>

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

constexpr std::string_view kPeer = "127.0.0.1:12345";
constexpr Byte kByteA = 0x0a;
constexpr Byte kByteB = 0x0b;
static_assert(kByteA != kByteB);
static constexpr size_t kLen = 128 * 1024;

class SimpleTest : public testing::Test {
 protected:
  SimpleTest() : l_(kLen, kByteA), r_(kLen, kByteB) {
    CHECK_EQ(l_.DataSize(), r_.DataSize());
    CHECK_NE(l_.Data(), r_.Data());
  }

  void WaitForCompletion(Transport& t, const Handle h) {
    while (true) {
      ASSERT_OK_AND_ASSIGN(const Status s, t.Poll(h));
      if (IsCompleted(s)) {
        EXPECT_EQ(s, Status::kSuccess);
        break;
      }
      absl::SleepFor(absl::Milliseconds(100));
    }
  }

 protected:
  UserApplication l_;
  UserApplication r_;
};

TEST_F(SimpleTest, Read) {
  // Pre-condition: no single local byte is equal to the remote.
  ASSERT_THAT(l_.Data(), Pointwise(Ne(), r_.Data()));

  // Local: post a read request.
  Transport& lt = l_.GetTransport();
  const Request& req = {
      .op = Op::kRead,
      .laddr = l_.DataPtr(),
      .raddr = r_.DataPtr(),
      .len = l_.DataSize(),
  };
  ASSERT_OK_AND_ASSIGN(const Handle h, lt.Post(kPeer, req));

  // Local: wait for the transport to finish processing the request.
  WaitForCompletion(lt, h);

  // Post-condition: all the local bytes are equal to the remote.
  EXPECT_THAT(l_.Data(), Pointwise(Eq(), r_.Data()));
  EXPECT_THAT(l_.Data(), Each(Eq(kByteB)));
}

TEST_F(SimpleTest, Write) {
  // Pre-condition: no single remote byte is equal to the local.
  ASSERT_THAT(r_.Data(), Pointwise(Ne(), l_.Data()));

  // Local: post a write request.
  Transport& lt = l_.GetTransport();
  const Request& req = {
      .op = Op::kWrite,
      .laddr = l_.DataPtr(),
      .raddr = r_.DataPtr(),
      .len = l_.DataSize(),
  };
  ASSERT_OK_AND_ASSIGN(const Handle h, lt.Post(kPeer, req));

  // Local: wait for the transport to finish processing the request.
  WaitForCompletion(lt, h);

  // Post-condition: all the remote bytes are equal to the local.
  EXPECT_THAT(r_.Data(), Pointwise(Eq(), l_.Data()));
  EXPECT_THAT(r_.Data(), Each(Eq(kByteA)));
}

}  // namespace
}  // namespace peregrine::integration_test
