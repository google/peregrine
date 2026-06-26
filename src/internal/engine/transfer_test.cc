#include "src/internal/engine/transfer.h"

#include <sys/socket.h>

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <memory>
#include <string>
#include <thread>  // NOLINT
#include <tuple>
#include <vector>

#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "absl/log/check.h"
#include "absl/log/log.h"
#include "absl/random/random.h"
#include "absl/strings/str_cat.h"
#include "src/api/transport_types.h"
#include "src/internal/assumptions.h"
#include "src/internal/base/types.h"
#include "src/internal/buffer/buffer_tracker.h"
#include "src/internal/channel/channel.h"
#include "src/internal/channel/channel_test_util.h"
#include "src/internal/chunk/chunk.h"
#include "src/util/util.h"

namespace peregrine::internal::testing {
namespace {

using ChannelType::kReliableStream;
using ChannelType::kUnreliableMessage;
using ::testing::Eq;
using ::testing::Ne;
using ::testing::Pointwise;
using ::testing::TestParamInfo;

static_assert(assumptions::kBufferIsDividedIntoFixedSizeChunks);
constexpr Handle kHandle(0x1234);
constexpr Buffer kBuffer(0xbeef);
constexpr uint32_t kNumChunks = 100;
constexpr uint32_t kChunkSize = 16;
constexpr uint32_t kLastChunkSize = kChunkSize - 1;
constexpr size_t kBufSize = (kNumChunks - 1) * kChunkSize + kLastChunkSize;
static_assert(kBufSize % kNumChunks != 0);

using TestParams = std::tuple<ChannelType>;

std::string ToString(const TestParamInfo<TestParams>& info) {
  const ChannelType type = std::get<0>(info.param);
  switch (type) {
    case kReliableStream:
      return absl::StrCat("Channel_ReliableStream");
    case kUnreliableMessage:
      return absl::StrCat("Channel_UnreliableMessage");
    default:
      DCHECK(false) << "Unreachable";
  }
}

class TransferTest : public ::testing::TestWithParam<TestParams> {
 protected:
  TransferTest()
      : src_(kBufSize),
        dst_(kBufSize),
        send_(),
        recv_(),
        sndr_(send_, recv_),
        rcvr_(send_, recv_) {
    for (int i = 0; i < kBufSize; ++i) {
      src_[i] = util::Random<Byte>(bitgen_, 0x01, 0xff);
      dst_[i] = static_cast<Byte>(0);
    }
  }

  static uint32_t GetChunkSize(uint32_t i) {
    DCHECK_LT(i, kNumChunks);
    return i != kNumChunks - 1 ? kChunkSize : kLastChunkSize;
  }

  ChunkMetadata GenChunk(uint32_t i) {
    DCHECK_LT(i, kNumChunks);
    return ChunkMetadata{
        .handle = kHandle,
        .buffer = kBuffer,
        .nchunks = kNumChunks,
        .index = chunk_t(i),
        .addr =
            addr_t(reinterpret_cast<uintptr_t>(dst_.data() + i * kChunkSize)),
        .size = GetChunkSize(i)};
  }

  ChunkPayloadView GenPayload(uint32_t i) {
    return ChunkPayloadView(src_.data() + i * kChunkSize, GetChunkSize(i));
  }

  static ConnectedChannelPair CreateChannelPair(ChannelType type) {
    if (type == kReliableStream) {
      return ConnectedChannelPair::CreateTcp(AF_INET);
    } else {
      DCHECK_EQ(type, kUnreliableMessage);
      return ConnectedChannelPair::CreateUdp(AF_INET);
    }
  }

 protected:
  absl::BitGen bitgen_;
  std::vector<Byte> src_;
  std::vector<Byte> dst_;
  BufferTracker send_;
  BufferTracker recv_;
  Transfer sndr_;
  Transfer rcvr_;
};

INSTANTIATE_TEST_SUITE_P(
    , TransferTest,
    ::testing::Combine(::testing::Values(kReliableStream, kUnreliableMessage)),
    ToString);

TEST_P(TransferTest, SendAndRecv) {
  // Precondition: dst is different from src.
  ASSERT_THAT(dst_, Pointwise(Ne(), src_));

  const auto param = GetParam();
  const ChannelType type = std::get<0>(param);
  const ConnectedChannelPair chs = CreateChannelPair(type);

  std::thread sndr([&]() {
    Channel* const channel = chs.sndr.get();
    while (!sndr_.IsSendDone(kHandle)) {
      for (uint32_t i = 0; i < kNumChunks; ++i) {
        const ChunkMetadata chunk = GenChunk(i);
        const ChunkPayloadView payload = GenPayload(i);
        CHECK(sndr_.SendChunk(channel, chunk, payload));
        sndr_.RecvChunk(channel);
      }
    }
  });
  std::thread rcvr([&]() {
    Channel* const channel = chs.rcvr.get();
    while (!rcvr_.IsRecvDone(kHandle)) {
      rcvr_.RecvChunk(channel);
    }
  });

  sndr.join();
  rcvr.join();

  EXPECT_TRUE(sndr_.IsSendDone(kHandle));
  EXPECT_TRUE(rcvr_.IsRecvDone(kHandle));

  // Postcondition: dst is the same as src.
  EXPECT_THAT(dst_, Pointwise(Eq(), src_));
}

}  // namespace
}  // namespace peregrine::internal::testing
