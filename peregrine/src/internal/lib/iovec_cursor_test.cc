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
  static constexpr size_t kTotal = 7 + 8 + 9;
  char a_[7];
  char b_[8];
  char c_[9];
  const std::array<IoVec, 3> iovs_;
};

TEST_F(IoVecCursorTest, Ctor) {
  IoVecCursor empty({});
  EXPECT_EQ(empty.Size(), 0);
  EXPECT_EQ(empty.Remaining(), 0);
  EXPECT_EQ(empty.Head(), nullptr);
  EXPECT_EQ(empty.Length(), 0);

  IoVecCursor c(iovs_);
  EXPECT_EQ(c.Size(), 3);
  EXPECT_EQ(c.Length(), kTotal);
  EXPECT_EQ(c.Remaining(), 3);
  EXPECT_EQ(c.Head()->iov_base, a_);
  EXPECT_EQ(c.Head()->iov_len, sizeof(a_));

  const IoVecCursor& cc = c;
  EXPECT_EQ(cc.Size(), 3);
  EXPECT_EQ(cc.Length(), kTotal);
  EXPECT_EQ(cc.Remaining(), 3);
  EXPECT_EQ(cc.Head()->iov_base, a_);
  EXPECT_EQ(cc.Head()->iov_len, sizeof(a_));
}

TEST_F(IoVecCursorTest, AdvanceInOneShot) {
  IoVecCursor c(iovs_);
  LOG(INFO) << c;

  EXPECT_TRUE(c.Advance(kTotal));
  EXPECT_EQ(c.Remaining(), 0);
  EXPECT_EQ(c.Head(), nullptr);
  EXPECT_EQ(c.Length(), kTotal);
  LOG(INFO) << c;

  EXPECT_DEBUG_DEATH(c.Advance(1), "out of range");
}

TEST_F(IoVecCursorTest, AdvanceGradually) {
  IoVecCursor c(iovs_);
  LOG(INFO) << c;

  // Zero advance is a no-op.
  EXPECT_FALSE(c.Advance(0));
  EXPECT_EQ(c.Remaining(), 3);
  EXPECT_EQ(c.Head()->iov_base, a_);
  EXPECT_EQ(c.Head()->iov_len, sizeof(a_));
  LOG(INFO) << c;

  // A few partial advances accumulate.
  EXPECT_FALSE(c.Advance(1));
  EXPECT_EQ(c.Remaining(), 3);
  EXPECT_EQ(c.Head()->iov_base, a_ + 1);
  EXPECT_EQ(c.Head()->iov_len, sizeof(a_) - 1);
  LOG(INFO) << c;

  EXPECT_FALSE(c.Advance(2));
  EXPECT_EQ(c.Remaining(), 3);
  EXPECT_EQ(c.Head()->iov_base, a_ + 3);
  EXPECT_EQ(c.Head()->iov_len, sizeof(a_) - 3);
  LOG(INFO) << c;

  EXPECT_FALSE(c.Advance(3));
  EXPECT_EQ(c.Remaining(), 3);
  EXPECT_EQ(c.Head()->iov_base, a_ + 6);
  EXPECT_EQ(c.Head()->iov_len, sizeof(a_) - 6);
  LOG(INFO) << c;

  // Advance to the next iovec's beginning.
  EXPECT_FALSE(c.Advance(1));
  EXPECT_EQ(c.Remaining(), 2);
  EXPECT_EQ(c.Head()->iov_base, b_ + 0);
  EXPECT_EQ(c.Head()->iov_len, sizeof(b_) - 0);
  LOG(INFO) << c;

  // Advance to the next iovec's middle.
  EXPECT_FALSE(c.Advance(10));
  EXPECT_EQ(c.Remaining(), 1);
  EXPECT_EQ(c.Head()->iov_base, c_ + 2);
  EXPECT_EQ(c.Head()->iov_len, sizeof(c_) - 2);
  LOG(INFO) << c;

  // Advance to the end.
  EXPECT_TRUE(c.Advance(7));
  EXPECT_EQ(c.Remaining(), 0);
  EXPECT_EQ(c.Head(), nullptr);
  LOG(INFO) << c;

  // Advance beyond the end.
  EXPECT_DEBUG_DEATH(c.Advance(1), "out of range");
  EXPECT_EQ(c.Remaining(), 0);
  EXPECT_EQ(c.Length(), kTotal);
  LOG(INFO) << c;
}

TEST_F(IoVecCursorTest, AdvanceOneByteAtATime) {
  IoVecCursor c(iovs_);
  for (size_t i = 1; i <= kTotal; ++i) {
    EXPECT_EQ(c.Advance(1), i >= kTotal) << "after " << i << " bytes";
    const size_t remaining = 3 - (i >= 7) - (i >= 15) - (i >= 24);
    EXPECT_EQ(c.Remaining(), remaining);
    LOG(INFO) << c;
  }
  EXPECT_EQ(c.Length(), kTotal);
}

}  // namespace
}  // namespace peregrine::internal::testing
