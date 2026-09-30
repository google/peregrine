#include "peregrine/src/internal/lib/iovec_view.h"

#include <array>
#include <cstddef>
#include <string_view>

#include "gtest/gtest.h"
#include "peregrine/src/internal/base/types.h"

namespace peregrine::internal::testing {
namespace {

class IoVecViewTest : public ::testing::Test {
 protected:
  IoVecViewTest()
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

TEST_F(IoVecViewTest, Ctor) {
  IoVecView empty({});
  EXPECT_EQ(empty.Size(), 0);
  EXPECT_EQ(empty.Remaining(), 0);
  EXPECT_EQ(empty.Head(), nullptr);

  IoVecView view(iovs_);
  EXPECT_EQ(view.Size(), 3);
  EXPECT_EQ(view.Remaining(), 3);
  EXPECT_EQ(view.Head()->iov_base, a_);
  EXPECT_EQ(view.Head()->iov_len, sizeof(a_));

  const IoVecView& const_view = view;
  EXPECT_EQ(const_view.Size(), 3);
  EXPECT_EQ(const_view.Remaining(), 3);
  EXPECT_EQ(const_view.Head()->iov_base, a_);
  EXPECT_EQ(const_view.Head()->iov_len, sizeof(a_));
}

TEST_F(IoVecViewTest, AdvanceInOneShot) {
  IoVecView view(iovs_);

  EXPECT_TRUE(view.Advance(kTotal));
  EXPECT_EQ(view.Remaining(), 0);
  EXPECT_EQ(view.Head(), nullptr);

  EXPECT_DEBUG_DEATH(view.Advance(1), "out of range");
}

TEST_F(IoVecViewTest, AdvanceGradually) {
  IoVecView view(iovs_);

  // Zero advance is a no-op.
  EXPECT_FALSE(view.Advance(0));
  EXPECT_EQ(view.Remaining(), 3);
  EXPECT_EQ(view.Head()->iov_base, a_);
  EXPECT_EQ(view.Head()->iov_len, sizeof(a_));

  // A few partial advances accumulate.
  EXPECT_FALSE(view.Advance(1));
  EXPECT_EQ(view.Remaining(), 3);
  EXPECT_EQ(view.Head()->iov_base, a_ + 1);
  EXPECT_EQ(view.Head()->iov_len, sizeof(a_) - 1);

  EXPECT_FALSE(view.Advance(2));
  EXPECT_EQ(view.Remaining(), 3);
  EXPECT_EQ(view.Head()->iov_base, a_ + 3);
  EXPECT_EQ(view.Head()->iov_len, sizeof(a_) - 3);

  EXPECT_FALSE(view.Advance(3));
  EXPECT_EQ(view.Remaining(), 3);
  EXPECT_EQ(view.Head()->iov_base, a_ + 6);
  EXPECT_EQ(view.Head()->iov_len, sizeof(a_) - 6);

  // Advance to the next iovec's beginning.
  EXPECT_FALSE(view.Advance(1));
  EXPECT_EQ(view.Remaining(), 2);
  EXPECT_EQ(view.Head()->iov_base, b_ + 0);
  EXPECT_EQ(view.Head()->iov_len, sizeof(b_) - 0);

  // Advance to the next iovec's middle.
  EXPECT_FALSE(view.Advance(10));
  EXPECT_EQ(view.Remaining(), 1);
  EXPECT_EQ(view.Head()->iov_base, c_ + 2);
  EXPECT_EQ(view.Head()->iov_len, sizeof(c_) - 2);

  // Advance to the end.
  EXPECT_TRUE(view.Advance(7));
  EXPECT_EQ(view.Remaining(), 0);
  EXPECT_EQ(view.Head(), nullptr);

  // Advance beyond the end.
  EXPECT_DEBUG_DEATH(view.Advance(1), "out of range");
  EXPECT_EQ(view.Remaining(), 0);
}

TEST_F(IoVecViewTest, AdvanceOneByteAtATime) {
  IoVecView view(iovs_);
  for (size_t i = 1; i <= kTotal; ++i) {
    EXPECT_EQ(view.Advance(1), i >= kTotal) << "after " << i << " bytes";
    const size_t remaining = 3 - (i >= 7) - (i >= 15) - (i >= 24);
    EXPECT_EQ(view.Remaining(), remaining);
  }
}

}  // namespace
}  // namespace peregrine::internal::testing
