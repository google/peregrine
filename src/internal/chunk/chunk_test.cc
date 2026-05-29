#include "src/internal/chunk/chunk.h"

#include <cstddef>
#include <cstdint>

#include "gtest/gtest.h"
#include "absl/log/check.h"
#include "absl/log/log.h"
#include "src/api/types.h"

namespace peregrine::internal::testing {
namespace {

constexpr addr_t kPayloadSrcAddr(0x12340000);  // read from
constexpr addr_t kBufferBaseAddr(0xffff0000);  // write to
constexpr Buffer kBuffer(0xbeef);
constexpr uint32_t kChunkSize = 1024;
constexpr uint32_t kNumChunks = 10;
constexpr chunk_t kChunkIndex(1);
static_assert(kBufferBaseAddr != kPayloadSrcAddr);

class ChunkTest : public ::testing::Test {
 protected:
  ChunkTest()
      : chunk_(GenMetadata()),
        payload1_(GenPayload(kChunkSize)),
        payload2_(GenPayload(kChunkSize * 2)) {
    CHECK(chunk_.IsValid());
  }

  static ChunkMetadata GenMetadata() {
    return ChunkMetadata{
        .base_addr = kBufferBaseAddr,
        .buffer = kBuffer,
        .chunk_size = kChunkSize,
        .nchunks = kNumChunks,
        .index = kChunkIndex,
    };
  }

  static ChunkPayloadView GenPayload(size_t size) {
    const Byte* src_addr =
        reinterpret_cast<const Byte*>(kPayloadSrcAddr.value());
    return ChunkPayloadView(src_addr, size);
  }

 protected:
  ChunkMetadata chunk_;
  ChunkPayloadView payload1_;
  ChunkPayloadView payload2_;
};

TEST_F(ChunkTest, ChunkAddr) {
  EXPECT_EQ(chunk_.DstAddr(), reinterpret_cast<Byte*>(0xffff'0400));
  EXPECT_TRUE(IsMatch(chunk_, payload1_));
  EXPECT_FALSE(IsMatch(chunk_, payload2_));
  LOG(INFO) << chunk_;
}

}  // namespace
}  // namespace peregrine::internal::testing
