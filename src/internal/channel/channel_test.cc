#include "src/internal/channel/channel.h"

#include <sys/socket.h>

#include <cstddef>
#include <memory>
#include <utility>
#include <vector>

#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "absl/log/check.h"
#include "absl/log/log.h"
#include "absl/types/span.h"
#include "src/api/transport_types.h"
#include "src/internal/channel/channel_test_util.h"
#include "src/internal/channel/channel_types.h"
#include "src/util/util.h"

namespace peregrine::internal::testing {
namespace {

using ::testing::Eq;
using ::testing::Ne;
using ::testing::Pointwise;

TEST(ReliableStreamChannelTest, ReadWrite) {
  // Create channel pairs.
  ConnectedChannelPair tcp = ConnectedChannelPair::CreateTcp(AF_INET);
  ConnectedChannelPair mem = ConnectedChannelPair::CreateMemStream();

  // Get the channel pointers.
  std::pair<Channel*, Channel*> tcp_chs = {tcp.sndr.get(), tcp.rcvr.get()};
  std::pair<Channel*, Channel*> mem_chs = {mem.sndr.get(), mem.rcvr.get()};

  // Data size and # of reads/writes.
  constexpr size_t kDataSize = 1UL << 20;
  constexpr int kSrc = 2;
  constexpr int kSink = kSrc * 2;
  static_assert(kSrc != kSink);
  static_assert(kDataSize % kSrc == 0);
  static_assert(kDataSize % kSink == 0);

  // Read and write on the channel pair.
  for (auto [sndr, rcvr] : {tcp_chs, mem_chs}) {
    ASSERT_TRUE(IsReliableStream(sndr->Type()));
    ASSERT_TRUE(IsReliableStream(rcvr->Type()));

    // Prepare input data and output.
    std::vector<Byte> src(kDataSize, 0);
    std::vector<Byte> sink(src.size(), 0);
    util::RandomNonZero(absl::MakeSpan(src));
    ASSERT_THAT(sink, Pointwise(Ne(), src));

    // Send to one channel a number of times.
    for (int i = 0; i < kSrc; ++i) {
      constexpr size_t kPart = kDataSize / kSrc;
      EXPECT_TRUE(sndr->Write({{src.data() + i * kPart, kPart}}));
    }

    // Receive from the other channel for a different number of times.
    for (int i = 0; i < kSink; ++i) {
      constexpr size_t kPart = kDataSize / kSink;
      EXPECT_EQ(rcvr->Read(sink.data() + i * kPart, kPart), kPart);
    }

    // Check that the data read is the same as written.
    EXPECT_THAT(sink, Pointwise(Eq(), src));

    // Shutdown the channels.
    sndr->Shutdown();
    rcvr->Shutdown();

    // Verify post-shutdown behavior.
    constexpr size_t kLen = 1;
    EXPECT_FALSE(sndr->Write({{src.data(), kLen}}));
    EXPECT_EQ(rcvr->Read(sink.data(), kLen), 0);

    LOG(INFO) << *sndr;
    LOG(INFO) << *rcvr;
  }
}

TEST(UnreliableMessageChannelTest, ReadWrite) {
  // Create channel pairs.
  ConnectedChannelPair udp = ConnectedChannelPair::CreateUdp(AF_INET6);
  ConnectedChannelPair mem = ConnectedChannelPair::CreateMemMsg(/*error_rt=*/0);

  // Get the channel pointers.
  std::pair<Channel*, Channel*> udp_chs = {udp.sndr.get(), udp.rcvr.get()};
  std::pair<Channel*, Channel*> mem_chs = {mem.sndr.get(), mem.rcvr.get()};

  // Read and write on the channel pair.
  for (auto [sndr, rcvr] : {udp_chs, mem_chs}) {
    ASSERT_TRUE(IsUnreliableMessage(sndr->Type()));
    ASSERT_TRUE(IsUnreliableMessage(rcvr->Type()));

    // Prepare input data and output.
    constexpr size_t kMsgSize = 128;
    std::vector<Byte> src(kMsgSize, 0);
    std::vector<Byte> sink(src.size(), 0);
    util::RandomNonZero(absl::MakeSpan(src));
    ASSERT_THAT(sink, Pointwise(Ne(), src));

    // Write to one channel the message.
    EXPECT_TRUE(sndr->Write({{src.data(), kMsgSize}}));

    // Read from the other channel.
    EXPECT_EQ(rcvr->Read(sink.data(), kMsgSize), kMsgSize);

    // Check that the data read is the same as written.
    EXPECT_THAT(sink, Pointwise(Eq(), src));

    // Shutdown the channels.
    sndr->Shutdown();
    rcvr->Shutdown();

    // Verify post-shutdown behavior.
    constexpr size_t kLen = 1;
    EXPECT_FALSE(sndr->Write({{src.data(), kLen}}));
    EXPECT_EQ(rcvr->Read(sink.data(), kLen), 0);

    LOG(INFO) << *sndr;
    LOG(INFO) << *rcvr;
  }
}

TEST(UnreliableMessageChannelTest, ErrorRate) {
  constexpr int kErrorRate = 30;  // 30%
  ConnectedChannelPair mem = ConnectedChannelPair::CreateMemMsg(kErrorRate);
  Channel* sndr = mem.sndr.get();
  Channel* rcvr = mem.rcvr.get();

  constexpr int kNumMessages = 1000;
  constexpr size_t kMsgSize = 128;
  std::vector<Byte> src(kMsgSize, 1);
  std::vector<Byte> sink(kMsgSize, 0);

  int errors = 0;
  for (int i = 0; i < kNumMessages; ++i) {
    ASSERT_TRUE(sndr->Write({{src.data(), kMsgSize}}));
    if (rcvr->Read(sink.data(), kMsgSize) < 0) {
      errors++;
    }
  }

  const double actual = static_cast<double>(errors) / kNumMessages * 100.0;
  LOG(INFO) << "Actual error rate: " << actual << "%";

  // Verify actual error rate is within reasonable range (e.g., +/- 10%)
  EXPECT_NEAR(actual, kErrorRate, 10.0);
}

}  // namespace
}  // namespace peregrine::internal::testing
