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

namespace peregrine::testing {
namespace {

using ::testing::Each;
using ::testing::ElementsAreArray;
using ::testing::Eq;
using ::testing::Not;

constexpr Endpoint kPeer = "peer_hostname";
constexpr Byte kLocalByte = 1;
constexpr Byte kRemoteByte = 2;
static_assert(kLocalByte != kRemoteByte);
constexpr size_t kLen = 1024;

class UserProcess final {
 public:
  explicit UserProcess(const Byte v) : data_(kLen, v), transport_() {}
  absl::Span<Byte> Data() { return absl::MakeSpan(data_); }
  Transport& GetTransport() { return transport_; }

 private:
  std::vector<Byte> data_;
  TransportImpl transport_;
};

class TransportImplTest : public ::testing::Test {
 protected:
  TransportImplTest() : local_(kLocalByte), remote_(kRemoteByte) {
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

  static std::string Info(const Request& req, const Handle h, const Status s) {
    return absl::StrFormat("TransportImplTest: %s handle = 0x%x, status = %v",
                           ToString(req.op), h.value(), s);
  }

 protected:
  UserProcess local_;
  UserProcess remote_;
};

TEST_F(TransportImplTest, Read) {
  ASSERT_THAT(local_.Data(), Not(ElementsAreArray(remote_.Data())));

  // Use one thread to emulate a local process.
  std::thread local([this]() {
    Transport& t = local_.GetTransport();
    const Request& req = MakeRequest(Op::kRead);
    ASSERT_OK_AND_ASSIGN(const Handle h, t.Post(kPeer, req));
    while (true) {
      ASSERT_OK_AND_ASSIGN(const Status s, t.Poll(h));
      LOG(INFO) << Info(req, h, s);
      if (IsCompleted(s)) break;
      absl::SleepFor(absl::Milliseconds(100));
    }
  });

  // Use another thread to emulate a remote process.
  std::thread remote([]() {
    // TODO(yongx): nothing needed yet.
  });

  absl::SleepFor(absl::Seconds(1));
  local.join();
  remote.join();

  EXPECT_THAT(local_.Data(), ElementsAreArray(remote_.Data()));
  EXPECT_THAT(local_.Data(), Each(Eq(kRemoteByte)));
}

TEST_F(TransportImplTest, Write) {
  ASSERT_THAT(remote_.Data(), Not(ElementsAreArray(local_.Data())));

  // Use one thread to emulate a local process.
  std::thread local([this]() {
    Transport& t = local_.GetTransport();
    const Request req = MakeRequest(Op::kWrite);
    ASSERT_OK_AND_ASSIGN(const Handle h, t.Post(kPeer, req));
    while (true) {
      ASSERT_OK_AND_ASSIGN(const Status s, t.Poll(h));
      LOG(INFO) << Info(req, h, s);
      if (IsCompleted(s)) break;
      absl::SleepFor(absl::Milliseconds(100));
    }
  });

  // Use another thread to emulate a remote process.
  std::thread remote([]() {
    // TODO(yongx): nothing needed yet.
  });

  absl::SleepFor(absl::Seconds(1));
  local.join();
  remote.join();

  EXPECT_THAT(remote_.Data(), ElementsAreArray(local_.Data()));
  EXPECT_THAT(remote_.Data(), Each(Eq(kLocalByte)));
}

}  // namespace
}  // namespace peregrine::testing
