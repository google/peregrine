#include <cstddef>
#include <string>
#include <thread>  // NOLINT

#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "absl/log/check.h"
#include "absl/log/log.h"
#include "absl/strings/str_format.h"
#include "absl/time/clock.h"
#include "absl/time/time.h"
#include "src/api/transport.h"
#include "src/api/types.h"
#include "src/internal/util/util.h"
#include "src/util/app.h"

namespace peregrine::internal::testing {
namespace {

using ::testing::Eq;
using ::testing::Ne;
using ::testing::Pointwise;

class TransportImplTest : public ::testing::Test {
  static constexpr size_t kBufSize = (1UL << 20) - 1;

 protected:
  TransportImplTest() : a_(kBufSize), b_(kBufSize) {
    DCHECK_EQ(a_.DataSize(), b_.DataSize());
  }

  static std::string Info(const Request& req, const Handle h, const Status s) {
    return absl::StrFormat(
        "TransportImplTest: %s handle = 0x%x, status = %s @ thread #%s",
        ToString(req.op), h.value(), ToString(s), ThreadId());
  }

 protected:
  util::App a_;
  util::App b_;
};

TEST_F(TransportImplTest, Read) {
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
    ASSERT_OK_AND_ASSIGN(const Handle h, t.Post(peer, {req}));
    while (true) {
      ASSERT_OK_AND_ASSIGN(const Status s, t.Poll(h));
      LOG(INFO) << Info(req, h, s);
      if (IsCompleted(s)) break;
      absl::SleepFor(absl::Seconds(1));
    }
  });

  // Use another thread to emulate a remote process.
  std::thread b([]() {
    // TODO(yongx): nothing needed yet.
  });

  absl::SleepFor(absl::Seconds(1));
  a.join();
  b.join();

  EXPECT_THAT(a_.Data(), Pointwise(Eq(), b_.Data()));
}

TEST_F(TransportImplTest, Write) {
  a_.GenData();
  b_.ClearData();
  ASSERT_THAT(b_.Data(), Pointwise(Ne(), a_.Data()));

  // Use one thread to emulate a local process.
  std::thread a([this]() {
    Transport& t = a_.GetTransport();
    const std::string peer = b_.GetEndpoint();
    const Request req = {
        .op = Op::kWrite,
        .laddr = a_.DataPtr(),
        .raddr = b_.DataPtr(),
        .len = a_.DataSize(),
    };
    ASSERT_OK_AND_ASSIGN(const Handle h, t.Post(peer, {req}));
    while (true) {
      ASSERT_OK_AND_ASSIGN(const Status s, t.Poll(h));
      LOG(INFO) << Info(req, h, s);
      if (IsCompleted(s)) break;
      absl::SleepFor(absl::Seconds(1));
    }
  });

  // Use another thread to emulate a remote process.
  std::thread b([]() {
    // TODO(yongx): nothing needed yet.
  });

  absl::SleepFor(absl::Seconds(1));
  a.join();
  b.join();

  EXPECT_THAT(b_.Data(), Pointwise(Eq(), a_.Data()));
}

}  // namespace
}  // namespace peregrine::internal::testing
