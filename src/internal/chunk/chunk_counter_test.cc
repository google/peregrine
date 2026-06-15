#include "src/internal/chunk/chunk_counter.h"

#include "gtest/gtest.h"
#include "src/internal/chunk/chunk.h"

namespace peregrine::internal::testing {
namespace {

TEST(ChunkCounterTest, OneWriter) {
  ChunkCounter counter(/*total_num_chunks=*/2);

  EXPECT_EQ(counter.TotalNumChunks(), 2);
  EXPECT_TRUE(counter.IsEmpty());
  EXPECT_FALSE(counter.IsCompleted());

  EXPECT_TRUE(counter.Acquire(chunk_t(0)));
  EXPECT_FALSE(counter.IsBusy(chunk_t(0)));
  EXPECT_TRUE(counter.IsEmpty());
  EXPECT_FALSE(counter.IsCompleted());

  counter.Release(chunk_t(0), /*success=*/false);
  EXPECT_FALSE(counter.IsEmpty());
  EXPECT_FALSE(counter.IsCompleted());

  EXPECT_TRUE(counter.Acquire(chunk_t(1)));
  EXPECT_FALSE(counter.IsBusy(chunk_t(1)));
  EXPECT_FALSE(counter.IsEmpty());
  EXPECT_FALSE(counter.IsCompleted());

  counter.Release(chunk_t(1), /*success=*/true);
  EXPECT_FALSE(counter.IsEmpty());
  EXPECT_TRUE(counter.IsCompleted());
}

}  // namespace
}  // namespace peregrine::internal::testing
