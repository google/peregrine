#include "peregrine/src/internal/chunk/chunk_flatbuf.h"

#include <string>

#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "absl/container/flat_hash_set.h"
#include "absl/log/log.h"
#include "absl/random/random.h"
#include "peregrine/src/internal/chunk/chunk.h"
#include "peregrine/src/internal/chunk/chunk_test_util.h"
#include "peregrine/src/internal/trace/chunk_trace_wire.h"

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

TEST(ChunkSerializationTest, SerdeWithTrace) {
  ChunkHeader chunk = GenChunkHeader();
  chunk.trace.tx_send_mono_us = 123456789ULL;
  chunk.trace.rx_payload_us = 450U;
  chunk.trace.rx_acquire_us = 12U;
  chunk.trace.rx_worker_id = -7;
  chunk.trace.rx_chunk_status = 0;
  ASSERT_TRUE(chunk.IsValid());
  ASSERT_TRUE(chunk.trace.IsTraced());

  const std::string s = ChunkUtil::Serialize(chunk);
  ASSERT_EQ(s.size(), ChunkUtil::kSize);

  ChunkHeader output;
  ASSERT_TRUE(ChunkUtil::Deserialize(s, output));
  EXPECT_EQ(chunk, output);
  EXPECT_EQ(output.trace.tx_send_mono_us, 123456789ULL);
  EXPECT_EQ(output.trace.rx_payload_us, 450U);
  EXPECT_EQ(output.trace.rx_acquire_us, 12U);
  EXPECT_EQ(output.trace.rx_worker_id, -7);
  EXPECT_EQ(output.trace.rx_chunk_status, 0U);
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
