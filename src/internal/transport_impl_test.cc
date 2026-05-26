#include "src/internal/transport_impl.h"

#include <cstddef>
#include <string>
#include <thread>  // NOLINT
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
#include "src/api/types.h"
#include "src/internal/util/util.h"

namespace peregrine::testing {
namespace {

using ::testing::Each;
using ::testing::Eq;
using ::testing::Ne;
using ::testing::Pointwise;

constexpr Endpoint kPeer = "peer_hostname";
constexpr Byte kByteA = 1;
constexpr Byte kByteB = 2;
static_assert(kByteA != kByteB);
constexpr size_t kLen = 1024;

class UserApplication final {
 public:
  explicit UserApplication(const Byte v) : data_(kLen, v), transport_() {}
  Byte* DataPtr() { return data_.data(); }
  size_t DataSize() const { return data_.size(); }
  absl::Span<const Byte> Data() const { return absl::MakeConstSpan(data_); }
  Transport& GetTransport() { return transport_; }

 private:
  std::vector<Byte> data_;
  TransportImpl transport_;
};

class TransportImplTest : public ::testing::Test {
 protected:
  TransportImplTest() : a_(kByteA), b_(kByteB) {
    CHECK_EQ(a_.DataSize(), b_.DataSize());
    CHECK_NE(a_.Data(), b_.Data());
  }

  static std::string Info(const Request& req, const Handle h, const Status s) {
    return absl::StrFormat(
        "TransportImplTest: %s handle = 0x%x, status = %s @ thread #%s",
        ToString(req.op), h.value(), ToString(s), ThreadId());
  }

 protected:
  UserApplication a_;
  UserApplication b_;
};

TEST_F(TransportImplTest, Read) {
  ASSERT_THAT(a_.Data(), Pointwise(Ne(), b_.Data()));

  // Use one thread to emulate a local process.
  std::thread a([this]() {
    Transport& t = a_.GetTransport();
    const Request req = {
        .op = Op::kRead,
        .laddr = a_.DataPtr(),
        .raddr = b_.DataPtr(),
        .len = a_.DataSize(),
    };
    ASSERT_OK_AND_ASSIGN(const Handle h, t.Post(kPeer, req));
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
  EXPECT_THAT(a_.Data(), Each(Eq(kByteB)));
}

TEST_F(TransportImplTest, Write) {
  ASSERT_THAT(b_.Data(), Pointwise(Ne(), a_.Data()));

  // Use one thread to emulate a local process.
  std::thread a([this]() {
    Transport& t = a_.GetTransport();
    const Request req = {
        .op = Op::kWrite,
        .laddr = a_.DataPtr(),
        .raddr = b_.DataPtr(),
        .len = a_.DataSize(),
    };
    ASSERT_OK_AND_ASSIGN(const Handle h, t.Post(kPeer, req));
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
  EXPECT_THAT(b_.Data(), Each(Eq(kByteA)));
}

}  // namespace
}  // namespace peregrine::testing
