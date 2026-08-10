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
  const ChunkHeader chunk = GenChunkHeader();
  ASSERT_TRUE(chunk.IsValid());

  const std::string s = ChunkUtil::Serialize(chunk);
  ASSERT_EQ(s.size(), ChunkUtil::kSize);

  ASSERT_EQ(s[0], 'P');
  ASSERT_EQ(s[1], 'G');

  ChunkHeader output;
  ASSERT_NE(chunk, output);
  ASSERT_TRUE(ChunkUtil::Deserialize(s, output));
  EXPECT_EQ(chunk, output);
}

TEST(ChunkSerializationTest, FixedSize) {
  absl::flat_hash_set<std::string> ss;
  constexpr int kRounds = 1'000'000;
  absl::BitGen bitgen;

  int round = 0;
  for (int i = 0; i < kRounds; ++i, round = i) {
    ChunkHeader chunk;
    GenChunkHeader(bitgen, chunk);
    ASSERT_TRUE(chunk.IsValid());

    const std::string s = ChunkUtil::Serialize(chunk);
    ASSERT_EQ(s.size(), ChunkUtil::kSize);

    ChunkHeader output;
    ASSERT_NE(chunk, output);
    ASSERT_TRUE(ChunkUtil::Deserialize(s, output));
    ASSERT_THAT(chunk, ::testing::Eq(output));

    ss.insert(s);
  }
  ASSERT_EQ(round, kRounds);
  ASSERT_GT(ss.size(), kRounds / 2);

  LOG(INFO) << "chunk header serialization rounds: " << round;
  LOG(INFO) << "chunk header serialization unique: " << ss.size();
}

}  // namespace
}  // namespace peregrine::internal::testing
