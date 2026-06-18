#include "src/internal/chunk/chunk_flatbuf.h"

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "absl/container/flat_hash_set.h"
#include "absl/log/log.h"
#include "absl/random/random.h"
#include "src/internal/chunk/chunk.h"
#include "src/internal/chunk/chunk_test_util.h"

namespace peregrine::internal::testing {
namespace {

TEST(ChunkSerializationTest, Serde) {
  ChunkMetadata chunk = GenChunkMetadata();
  ASSERT_TRUE(chunk.IsValid());

  const std::string s = ChunkHeader::Serialize(chunk);
  ASSERT_EQ(s.size(), ChunkHeader::kSize);

  ASSERT_EQ(s[0], 0x70);  // 'p' in ascii
  ASSERT_EQ(s[1], 0x67);  // 'g' in ascii

  ChunkMetadata m1;
  ASSERT_NE(chunk, m1);
  ASSERT_TRUE(ChunkHeader::Deserialize(s, m1));
  EXPECT_EQ(chunk, m1);
}

TEST(ChunkSerializationTest, FromV1) {
  const std::vector<uint8_t> v1 = {
      112, 103, 1, 0, 52, 18, 0, 0, 239, 190, 0,   0,   10, 0, 0, 0,
      1,   0,   0, 0, 0,  4,  0, 0, 0,   4,   255, 255, 0,  0, 0, 0,
      0,   0,   0, 0, 0,  0,  0, 0, 0,   0,   0,   0,   0,  0, 0, 0,
      0,   0,   0, 0, 0,  0,  0, 0, 0,   0,   0,   0,   0,  0, 0, 0};
  ASSERT_EQ(v1.size(), ChunkHeader::kSize);

  std::string_view s(reinterpret_cast<const char*>(v1.data()), v1.size());

  ChunkMetadata m1;
  ASSERT_TRUE(ChunkHeader::Deserialize(s, m1));
  EXPECT_EQ(m1.handle, kHandle);
  EXPECT_EQ(m1.buffer, kBuffer);
  EXPECT_EQ(m1.nchunks, kNumChunks);
  EXPECT_EQ(m1.index, kChunkIndex);
  EXPECT_EQ((m1.addr - kBufferBaseAddr).value(), kChunkSize);
  EXPECT_EQ(m1.size, kChunkSize);
}

TEST(ChunkSerializationTest, FixedSize) {
  absl::flat_hash_set<std::string> ss;
  constexpr int kRounds = 1000'000;
  absl::BitGen bitgen;

  int round = 0;
  for (int i = 0; i < kRounds; ++i, round = i) {
    ChunkMetadata chunk;
    GenChunkMetadata(bitgen, chunk);
    ASSERT_TRUE(chunk.IsValid());

    const std::string s = ChunkHeader::Serialize(chunk);
    ASSERT_EQ(s.size(), ChunkHeader::kSize);

    ChunkMetadata m;
    ASSERT_NE(chunk, m);
    ASSERT_TRUE(ChunkHeader::Deserialize(s, m));
    ASSERT_THAT(chunk, ::testing::Eq(m));

    ss.insert(s);
  }
  ASSERT_EQ(round, kRounds);
  ASSERT_GT(ss.size(), kRounds / 2);

  LOG(INFO) << "chunk metadata serialization rounds: " << round;
  LOG(INFO) << "chunk metadata serialization unique: " << ss.size();
}

}  // namespace
}  // namespace peregrine::internal::testing
