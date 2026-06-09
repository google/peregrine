#include "src/internal/engine/transfer.h"

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <memory>
#include <thread>  // NOLINT
#include <vector>

#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "absl/log/check.h"
#include "absl/random/random.h"
#include "src/api/types.h"
#include "src/internal/assumptions.h"
#include "src/internal/channel/channel.h"
#include "src/internal/channel/channel_test_util.h"
#include "src/internal/chunk/chunk.h"
#include "src/internal/chunk/chunk_tracker.h"
#include "src/util/util.h"

namespace peregrine::internal::testing {
namespace {

using ChannelType::kReliableStream;
using ChannelType::kUnreliableMessage;
using ::testing::Eq;
using ::testing::Ne;
using ::testing::Pointwise;

static_assert(assumptions::kBufferIsDividedIntoFixedSizeChunks);
constexpr Handle kHandle(0x1234);
constexpr Buffer kBuffer(0xbeef);
constexpr uint32_t kNumChunks = 1024;
constexpr uint32_t kChunkSize = 16;
constexpr size_t kBufSize = kNumChunks * kChunkSize;

template <typename T>
T* Ptr(ChunkMetadata& metadata) {
  return reinterpret_cast<T*>(&metadata);
}

class TransferTest : public ::testing::Test {
 protected:
  TransferTest() : src_(kBufSize), dst_(kBufSize), chunk_tracker_(kNumChunks) {
    DCHECK(chunk_tracker_.IsEmpty());
    for (int i = 0; i < kBufSize; ++i) {
      src_[i] = util::Random<Byte>(bitgen_, 0x01, 0xff);
      dst_[i] = static_cast<Byte>(0);
    }
  }

  ChunkMetadata GenChunk(uint32_t i) {
    return ChunkMetadata{
        .base_addr = addr_t(reinterpret_cast<uintptr_t>(dst_.data())),
        .handle = kHandle,
        .buffer = kBuffer,
        .size = kChunkSize,
        .nchunks = kNumChunks,
        .index = chunk_t(i)};
  }

  ChunkPayloadView GenPayload(uint32_t i) {
    return ChunkPayloadView(src_.data() + i * kChunkSize, kChunkSize);
  }

  static std::unique_ptr<Channel> CreateChannel(ChannelType type) {
    if (type == kReliableStream) {
      return TestOnly_CreateMemStreamChannel();
    } else {
      DCHECK_EQ(type, kUnreliableMessage);
      return TestOnly_CreateMemMsgChannel(/*error_rate=*/0);
    }
  }

 protected:
  absl::BitGen bitgen_;
  std::vector<Byte> src_;
  std::vector<Byte> dst_;
  ChunkTracker chunk_tracker_;
};

TEST_F(TransferTest, SendAndRecv) {
  // Precondition: dst is different from src.
  ASSERT_THAT(dst_, Pointwise(Ne(), src_));

  // Set up chunk tracker lookup.
  auto lookup = [this](Handle, Buffer) { return &chunk_tracker_; };

  Transfer xfer;
  for (const auto type : {kReliableStream, kUnreliableMessage}) {
    std::unique_ptr<Channel> ch = CreateChannel(type);
    Channel* const channel = ch.get();
    ASSERT_NE(channel, nullptr);

    std::thread sndr([&]() {
      for (uint32_t i = 0; i < kNumChunks; ++i) {
        const ChunkMetadata chunk = GenChunk(i);
        const ChunkPayloadView payload = GenPayload(i);
        CHECK(xfer.SendChunk(channel, chunk, payload));
      }
    });

    std::thread rcvr([&]() {
      while (!chunk_tracker_.IsCompleted()) {
        xfer.RecvChunk(channel, lookup);
      }
    });

    sndr.join();
    rcvr.join();

    // Postcondition: dst is the same as src.
    EXPECT_THAT(dst_, Pointwise(Eq(), src_));
  }
}

}  // namespace
}  // namespace peregrine::internal::testing
