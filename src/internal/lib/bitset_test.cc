#include "src/internal/lib/bitset.h"

#include <cstdint>

#include "gtest/gtest.h"
#include "absl/log/check.h"
#include "absl/log/log.h"
#include "absl/random/random.h"
#include "src/util/util.h"

namespace peregrine::internal::testing {
namespace {

TEST(BitsetTest, Basic) {
  constexpr uint32_t kSize = 100;
  Bitset m(kSize);
  ASSERT_TRUE(m.IsEmpty());
  ASSERT_EQ(m.Size(), kSize);
  ASSERT_EQ(m.Count(), 0);

  // Set half of the bits.
  for (int i = 0; i < kSize; i += 2) {
    m.Set(i);
    EXPECT_TRUE(m.Get(i));
  }
  EXPECT_EQ(m.Count(), kSize / 2);
  EXPECT_FALSE(m.IsEmpty());
  EXPECT_FALSE(m.IsFull());
  LOG(INFO) << m;

  // Clear all of them.
  for (int i = 0; i < kSize; i += 2) {
    m.Reset(i);
    EXPECT_FALSE(m.Get(i));
  }
  EXPECT_EQ(m.Count(), 0);
  EXPECT_TRUE(m.IsEmpty());
  LOG(INFO) << m;

  // Set all of them.
  for (int i = 0; i < kSize; ++i) {
    m.Set(i);
    EXPECT_TRUE(m.Get(i));
  }
  EXPECT_EQ(m.Count(), kSize);
  EXPECT_TRUE(m.IsFull());
  LOG(INFO) << m;
}

TEST(BitsetTest, ToString) {
  absl::BitGen bitgen;
  for (const uint32_t size : {10, 100, 1'000}) {
    Bitset m(size);
    ASSERT_EQ(m.Size(), size);
    for (int i = 0; i < size; ++i) {
      m.Set(util::Random<uint32_t>(bitgen, 0, size - 1));
      LOG_IF(INFO, i % (size / 10) == 0) << m;
    }
    LOG(INFO) << m;
  }
}

}  // namespace
}  // namespace peregrine::internal::testing
