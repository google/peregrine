#include "peregrine/src/internal/lib/iovec_cursor.h"

#include <array>
#include <cstddef>

#include "gtest/gtest.h"
#include "absl/log/log.h"
#include "peregrine/src/internal/base/types.h"

namespace peregrine::internal::testing {
namespace {

class IoVecCursorTest : public ::testing::Test {
 protected:
  IoVecCursorTest()
      : iovs_({IoVec{a_, sizeof(a_)}, IoVec{b_, sizeof(b_)},
               IoVec{c_, sizeof(c_)}}) {}

  static char* Base(const IoVec* vec) {
    return static_cast<char*>(vec->iov_base);
  }

 protected:
  static constexpr size_t kTotalItems = 3;
  static constexpr size_t kTotalBytes = 7 + 8 + 9;
  char a_[7];
  char b_[8];
  char c_[9];
  const std::array<IoVec, 3> iovs_;
};

TEST_F(IoVecCursorTest, Ctor) {
  EXPECT_DEBUG_DEATH(IoVecCursor empty({}), "");
  EXPECT_DEBUG_DEATH(IoVecCursor too_big({IoVec{a_, 1ULL << 63}}), "");

  IoVecCursor c(iovs_);
  EXPECT_EQ(c.TotalItems(), kTotalItems);
  EXPECT_EQ(c.TotalBytes(), kTotalBytes);
  EXPECT_EQ(c.RemainingItems(), kTotalItems);
  EXPECT_EQ(c.RemainingBytes(), kTotalBytes);
  EXPECT_EQ(c.Head()->iov_base, a_);
  EXPECT_EQ(c.Head()->iov_len, sizeof(a_));

  const IoVecCursor& cc = c;
  EXPECT_EQ(cc.TotalItems(), kTotalItems);
  EXPECT_EQ(cc.TotalBytes(), kTotalBytes);
  EXPECT_EQ(cc.RemainingItems(), kTotalItems);
  EXPECT_EQ(cc.RemainingBytes(), kTotalBytes);
  EXPECT_EQ(cc.Head()->iov_base, a_);
  EXPECT_EQ(cc.Head()->iov_len, sizeof(a_));
}

TEST_F(IoVecCursorTest, AdvanceInOneShot) {
  IoVecCursor c(iovs_);
  ASSERT_EQ(c.TotalItems(), kTotalItems);
  ASSERT_EQ(c.TotalBytes(), kTotalBytes);
  LOG(INFO) << c;

  EXPECT_TRUE(c.Advance(kTotalBytes));
  EXPECT_EQ(c.RemainingItems(), 0);
  EXPECT_EQ(c.RemainingBytes(), 0);
  EXPECT_EQ(c.Head(), nullptr);

  EXPECT_DEBUG_DEATH(c.Advance(1), "out of range");
  LOG(INFO) << c;
}

TEST_F(IoVecCursorTest, AdvanceGradually) {
  IoVecCursor c(iovs_);
  ASSERT_EQ(c.TotalItems(), kTotalItems);
  ASSERT_EQ(c.TotalBytes(), kTotalBytes);
  LOG(INFO) << c;

  // Zero advance is not allowed.
  EXPECT_DEBUG_DEATH(c.Advance(0), "zero byte");
  EXPECT_EQ(c.RemainingItems(), kTotalItems);
  EXPECT_EQ(c.RemainingBytes(), kTotalBytes);
  EXPECT_EQ(c.Head()->iov_base, a_);
  EXPECT_EQ(c.Head()->iov_len, sizeof(a_));
  LOG(INFO) << c;

  // A few partial advances accumulate.
  EXPECT_FALSE(c.Advance(1));
  EXPECT_EQ(c.RemainingItems(), 3);
  EXPECT_EQ(c.RemainingBytes(), kTotalBytes - 1);
  EXPECT_EQ(c.Head()->iov_base, a_ + 1);
  EXPECT_EQ(c.Head()->iov_len, sizeof(a_) - 1);
  LOG(INFO) << c;

  EXPECT_FALSE(c.Advance(2));
  EXPECT_EQ(c.RemainingItems(), 3);
  EXPECT_EQ(c.RemainingBytes(), kTotalBytes - 3);
  EXPECT_EQ(c.Head()->iov_base, a_ + 3);
  EXPECT_EQ(c.Head()->iov_len, sizeof(a_) - 3);
  LOG(INFO) << c;

  EXPECT_FALSE(c.Advance(3));
  EXPECT_EQ(c.RemainingItems(), 3);
  EXPECT_EQ(c.RemainingBytes(), kTotalBytes - 6);
  EXPECT_EQ(c.Head()->iov_base, a_ + 6);
  EXPECT_EQ(c.Head()->iov_len, sizeof(a_) - 6);
  LOG(INFO) << c;

  // Advance to the next iovec's beginning.
  EXPECT_FALSE(c.Advance(1));
  EXPECT_EQ(c.RemainingItems(), 2);
  EXPECT_EQ(c.RemainingBytes(), kTotalBytes - 7);
  EXPECT_EQ(c.Head()->iov_base, b_ + 0);
  EXPECT_EQ(c.Head()->iov_len, sizeof(b_) - 0);
  LOG(INFO) << c;

  // Advance to the next iovec's middle.
  EXPECT_FALSE(c.Advance(10));
  EXPECT_EQ(c.RemainingItems(), 1);
  EXPECT_EQ(c.RemainingBytes(), kTotalBytes - 17);
  EXPECT_EQ(c.Head()->iov_base, c_ + 2);
  EXPECT_EQ(c.Head()->iov_len, sizeof(c_) - 2);
  LOG(INFO) << c;

  // Advance to the end.
  EXPECT_TRUE(c.Advance(7));
  EXPECT_EQ(c.RemainingItems(), 0);
  EXPECT_EQ(c.RemainingBytes(), 0);
  EXPECT_EQ(c.Head(), nullptr);
  LOG(INFO) << c;

  // Advance beyond the end.
  EXPECT_DEBUG_DEATH(c.Advance(1), "out of range");
  LOG(INFO) << c;
}

TEST_F(IoVecCursorTest, AdvanceOneByteAtATime) {
  IoVecCursor c(iovs_);
  ASSERT_EQ(c.TotalItems(), kTotalItems);
  ASSERT_EQ(c.TotalBytes(), kTotalBytes);
  ASSERT_NE(c.Head(), nullptr);

  for (size_t i = 1; i <= kTotalBytes; ++i) {
    EXPECT_EQ(c.Advance(1), i >= kTotalBytes) << "after " << i << " bytes";
    const size_t remaining = 3 - (i >= 7) - (i >= 15) - (i >= 24);
    EXPECT_EQ(c.RemainingItems(), remaining);
    EXPECT_EQ(c.RemainingBytes(), kTotalBytes - i);
    LOG(INFO) << c;
  }

  EXPECT_EQ(c.TotalItems(), kTotalItems);
  EXPECT_EQ(c.TotalBytes(), kTotalBytes);
  EXPECT_EQ(c.Head(), nullptr);
}

}  // namespace
}  // namespace peregrine::internal::testing
