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
  constexpr int kIn = 2;
  constexpr int kOut = kIn * 2;
  static_assert(kIn != kOut);
  static_assert(kDataSize % kIn == 0);
  static_assert(kDataSize % kOut == 0);

  // Read and write on the channel pair.
  for (auto [sndr, rcvr] : {tcp_chs, mem_chs}) {
    ASSERT_TRUE(IsReliableStream(sndr->Type()));
    ASSERT_TRUE(IsReliableStream(rcvr->Type()));

    // Prepare input data and output.
    std::vector<Byte> in(kDataSize, 0);
    std::vector<Byte> out(in.size(), 0);
    util::RandomNonZero(absl::MakeSpan(in));
    ASSERT_THAT(out, Pointwise(Ne(), in));

    // Send to one channel a number of times.
    for (int i = 0; i < kIn; ++i) {
      constexpr size_t kPart = kDataSize / kIn;
      EXPECT_TRUE(sndr->Write({{in.data() + i * kPart, kPart}}));
    }

    // Receive from the other channel for a different number of times.
    for (int i = 0; i < kOut; ++i) {
      constexpr size_t kPart = kDataSize / kOut;
      EXPECT_EQ(rcvr->Read(out.data() + i * kPart, kPart), kPart);
    }

    // Check that the data read is the same as written.
    EXPECT_THAT(out, Pointwise(Eq(), in));

    // Shutdown the channels.
    sndr->Shutdown();
    rcvr->Shutdown();

    // Verify post-shutdown behavior.
    EXPECT_FALSE(sndr->Write({{in.data(), /*len=*/1}}));
    EXPECT_EQ(rcvr->Read(out.data(), /*len=*/1), 0);

    LOG(INFO) << *sndr;
    LOG(INFO) << *rcvr;
  }
}

TEST(UnreliableMessageChannelTest, ReadWrite) {
  // Create channel pairs.
  ConnectedChannelPair udp = ConnectedChannelPair::CreateUdp(AF_INET6);
  std::unique_ptr<Channel> mem = TestOnly_CreateMemMsgChannel(/*error=*/0);

  // Get the channel pointers.
  std::pair<Channel*, Channel*> udp_chs = {udp.sndr.get(), udp.rcvr.get()};
  std::pair<Channel*, Channel*> mem_chs = {mem.get(), mem.get()};

  // Read and write on the channel pair.
  for (auto [sndr, rcvr] : {udp_chs, mem_chs}) {
    ASSERT_TRUE(IsUnreliableMessage(sndr->Type()));
    ASSERT_TRUE(IsUnreliableMessage(rcvr->Type()));

    // Prepare input data and output.
    constexpr size_t kMsgSize = 128;
    std::vector<Byte> in(kMsgSize, 0);
    std::vector<Byte> out(in.size(), 0);
    util::RandomNonZero(absl::MakeSpan(in));
    ASSERT_THAT(out, Pointwise(Ne(), in));

    // Write to one channel the message.
    EXPECT_TRUE(sndr->Write({{in.data(), kMsgSize}}));

    // Read from the other channel twice.
    EXPECT_EQ(rcvr->Read(out.data(), kMsgSize), kMsgSize);

    // Check that the data read is the same as written.
    EXPECT_THAT(out, Pointwise(Eq(), in));

    // Shutdown the channels.
    sndr->Shutdown();
    rcvr->Shutdown();

    LOG(INFO) << *sndr;
    LOG(INFO) << *rcvr;
  }
}

}  // namespace
}  // namespace peregrine::internal::testing
