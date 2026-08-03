#include "src/internal/channel/channel.h"

#include <sys/socket.h>

#include <cstddef>
#include <memory>
#include <string>
#include <tuple>
#include <vector>

#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "absl/log/check.h"
#include "absl/log/log.h"
#include "absl/strings/str_format.h"
#include "absl/types/span.h"
#include "src/api/transport_types.h"
#include "src/internal/base/types.h"
#include "src/internal/channel/channel_test_util.h"
#include "src/util/util.h"

namespace peregrine::internal::testing {
namespace {

using ::testing::Eq;
using ::testing::Ne;
using ::testing::Pointwise;
using ::testing::Values;

using Param = std::tuple<TestChannelType, /*family=*/int, /*size=*/size_t>;

std::string FamilyToString(const int family) {
  if (family == AF_INET) return "IPv4";
  if (family == AF_INET6) return "IPv6";
  return "";
}

std::string ToString(const ::testing::TestParamInfo<Param>& info) {
  const TestChannelType type = std::get<0>(info.param);
  const int family = std::get<1>(info.param);
  const size_t size = std::get<2>(info.param);
  return absl::StrFormat("%s_%s_Size_%zu", ToString(type),
                         FamilyToString(family), size);
}

class ChannelTest : public ::testing::TestWithParam<Param> {
 protected:
  ChannelTest()
      : size_(std::get<2>(GetParam())),
        part_(size_ / 4),
        src_(size_),
        dst_(size_, 0) {
    util::RandomNonZero(absl::MakeSpan(src_));
    DCHECK_NE(src_.data(), dst_.data());

    CHECK_EQ(size_, 4 * part_);
    src_iov_ = {src_.data() + 0 * part_, part_};
    src_iovs_ = {{src_.data() + 1 * part_, part_},
                 {src_.data() + 2 * part_, part_},
                 {src_.data() + 3 * part_, part_}};
    dst_iov_ = {dst_.data() + 0 * part_, part_};
    dst_iovs_ = {{dst_.data() + 1 * part_, part_},
                 {dst_.data() + 2 * part_, 2 * part_}};
  }

 protected:
  const size_t size_;
  const size_t part_;
  std::vector<Byte> src_;
  std::vector<Byte> dst_;
  IoVec src_iov_;
  IoVec dst_iov_;
  std::vector<IoVec> src_iovs_;
  std::vector<IoVec> dst_iovs_;
};

INSTANTIATE_TEST_SUITE_P(
    , ChannelTest,
    Values(Param{TestChannelType::kTcp, AF_INET, /*size=*/1UL << 20},
           Param{TestChannelType::kTcp, AF_INET6, /*size=*/1UL << 20},
           Param{TestChannelType::kMemStream, 0, /*size=*/1UL << 20},
           Param{TestChannelType::kUdp, AF_INET, /*size=*/1UL << 10},
           Param{TestChannelType::kUdp, AF_INET6, /*size=*/1UL << 10},
           Param{TestChannelType::kMemMsg, 0, /*size=*/1UL << 10}),
    ToString);

TEST_P(ChannelTest, ReadWrite) {
  const auto param = GetParam();
  const TestChannelType type = std::get<0>(param);
  const int family = std::get<1>(param);
  const auto chs = CreateTestChannelPair(type, family, /*error_rate=*/0);
  Channel* sndr = chs.sndr.get();
  Channel* rcvr = chs.rcvr.get();

  // Precondition: dst is different from src_.
  ASSERT_THAT(dst_, Pointwise(Ne(), src_));

  // Send to one channel a number of times.
  EXPECT_EQ(sndr->Write((Byte*)src_iov_.iov_base, src_iov_.iov_len), part_);
  EXPECT_EQ(sndr->WriteV(src_iovs_), size_ - part_);

  // Receive from the other channel in a different way.
  EXPECT_EQ(rcvr->Read((Byte*)dst_iov_.iov_base, dst_iov_.iov_len), part_);
  EXPECT_EQ(rcvr->ReadV(absl::MakeSpan(dst_iovs_)), size_ - part_);

  // Shutdown the channels and verify post-shutdown behavior.
  sndr->Shutdown();
  rcvr->Shutdown();
  constexpr size_t kLen = 1;
  EXPECT_EQ(sndr->Write(src_.data(), kLen), -1);
  EXPECT_EQ(rcvr->Read(dst_.data(), kLen), 0);

  // Check that the data read is the same as written.
  EXPECT_THAT(dst_, Pointwise(Eq(), src_));

  LOG(INFO) << *sndr;
  LOG(INFO) << *rcvr;
}

TEST(UnreliableMessageChannelTest, ErrorRate) {
  constexpr int kErrorRate = 30;  // percentage
  ConnectedChannelPair mem = CreateMemMsgChannelPair(kErrorRate);
  Channel* sndr = mem.sndr.get();
  Channel* rcvr = mem.rcvr.get();

  constexpr int kNumMessages = 1000;
  constexpr size_t kMsgSize = 128;
  std::vector<Byte> src(kMsgSize, 1);
  std::vector<Byte> sink(kMsgSize, 0);

  int errors = 0;
  for (int i = 0; i < kNumMessages; ++i) {
    ASSERT_EQ(sndr->Write(src.data(), kMsgSize), kMsgSize);
    if (rcvr->Read(sink.data(), kMsgSize) != kMsgSize) ++errors;
  }
  for (int i = 0; i < kNumMessages; ++i) {
    ASSERT_EQ(sndr->WriteV({{src.data(), kMsgSize}}), kMsgSize);
    IoVec iovs[] = {{sink.data(), kMsgSize}};
    if (rcvr->ReadV(iovs) != kMsgSize) ++errors;
  }

  const double actual = 100.0 * errors / (2 * kNumMessages);
  LOG(INFO) << "Actual error rate: " << actual << "%";
  EXPECT_NEAR(actual, kErrorRate, 10.0);
}

}  // namespace
}  // namespace peregrine::internal::testing
