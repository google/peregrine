#include "src/internal/chunk/chunk_flatbuf.h"

#include <string>

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

  ChunkMetadata m1;
  ASSERT_NE(chunk, m1);
  ASSERT_TRUE(ChunkHeader::Deserialize(s, m1));
  EXPECT_EQ(chunk, m1);
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
