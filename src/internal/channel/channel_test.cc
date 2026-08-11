#include "src/internal/channel/channel.h"

#include <sys/socket.h>

#include <array>
#include <cstddef>
#include <memory>
#include <string>
#include <vector>

#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "absl/log/check.h"
#include "absl/log/log.h"
#include "absl/types/span.h"
#include "src/api/transport_types.h"
#include "src/internal/base/types.h"
#include "src/internal/channel/channel_test_util.h"
#include "src/internal/util/test_param.h"
#include "src/util/util.h"

namespace peregrine::internal::testing {
namespace {

using ::testing::Eq;
using ::testing::Ne;
using ::testing::Pointwise;
using ::testing::Values;

using Param = TestChannelSizeParam;

std::string ToString(const ::testing::TestParamInfo<Param>& info) {
  return testing::ToString(info.param);
}

class ChannelTest : public ::testing::TestWithParam<Param> {
 protected:
  ChannelTest()
      : size_(GetParam().size),
        part_(size_ / 4),
        src_(size_),
        dst_(size_, 0),
        siovs_({
            {src_.data() + 0 * part_, part_},
            {src_.data() + 1 * part_, part_},
            {src_.data() + 2 * part_, part_},
            {src_.data() + 3 * part_, part_},
        }),
        diovs_({
            {dst_.data() + 0 * part_, part_},
            {dst_.data() + 1 * part_, part_},
            {dst_.data() + 2 * part_, part_ * 2},
        }) {
    util::RandomNonZero(absl::MakeSpan(src_));
    DCHECK_NE(src_.data(), dst_.data());
    CHECK_EQ(size_, 4 * part_);
  }

 protected:
  const size_t size_;
  const size_t part_;
  std::vector<Byte> src_;
  std::vector<Byte> dst_;

  static constexpr int kSrc = 4;
  static constexpr int kDst = 3;
  std::array<IoVec, kSrc> siovs_;
  std::array<IoVec, kDst> diovs_;
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
  const auto p = GetParam();
  const auto chs = CreateTestChannelPair(p.type, p.family, /*error_rate=*/0);
  Channel* sndr = chs.sndr.get();
  Channel* rcvr = chs.rcvr.get();

  // Precondition: dst is different from src_.
  ASSERT_THAT(dst_, Pointwise(Ne(), src_));

  // Send to one channel a number of times.
  EXPECT_EQ(sndr->Write((Byte*)siovs_[0].iov_base, siovs_[0].iov_len), part_);
  EXPECT_EQ(sndr->WriteV(absl::MakeSpan(&siovs_[1], kSrc - 1)), size_ - part_);

  // Receive from the other channel in a different way.
  EXPECT_EQ(rcvr->Read((Byte*)diovs_[0].iov_base, diovs_[0].iov_len), part_);
  EXPECT_EQ(rcvr->ReadV(absl::MakeSpan(&diovs_[1], kDst - 1)), size_ - part_);

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
