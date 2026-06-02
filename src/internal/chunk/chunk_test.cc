#include "src/internal/chunk/chunk.h"

#include "gtest/gtest.h"
#include "absl/log/check.h"
#include "absl/log/log.h"
#include "src/api/types.h"
#include "src/internal/chunk/chunk_test_util.h"

namespace peregrine::internal::testing {
namespace {

class ChunkTest : public ::testing::Test {
 protected:
  ChunkTest()
      : chunk_(GenChunkMetadata(kChunkIndex)),
        payload1_(GenPayload(kChunkSize)),
        payload2_(GenPayload(kChunkSize - 1)) {
    CHECK(chunk_.IsValid());
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
