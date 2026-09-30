#include "peregrine/src/internal/lib/iovec_view.h"

#include <array>
#include <cstddef>
#include <memory>

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

TEST_F(IoVecViewTest, Basic) {
  IoVecView view(iovs_);
  EXPECT_EQ(view.size(), 3);
  EXPECT_EQ(Base(view[0]), a_);
  EXPECT_EQ(Base(view[1]), b_);
  EXPECT_EQ(Base(view[2]), c_);
  EXPECT_EQ(view[0]->iov_len, sizeof(a_));
  EXPECT_EQ(view[1]->iov_len, sizeof(b_));
  EXPECT_EQ(view[2]->iov_len, sizeof(c_));

  const IoVecView& const_view = view;
  EXPECT_EQ(const_view.size(), 3);
  EXPECT_EQ(Base(const_view[0]), a_);
  EXPECT_EQ(const_view[2]->iov_len, sizeof(c_));
}

TEST_F(IoVecViewTest, Empty) {
  IoVecView view({});
  EXPECT_EQ(view.size(), 0);
  EXPECT_EQ(view.Advance(0), 0);
  EXPECT_EQ(view.Advance(10), 0);
}

TEST_F(IoVecViewTest, AdvanceZeroIsNoop) {
  IoVecView view(iovs_);
  EXPECT_EQ(view.Advance(0), 0);
  EXPECT_EQ(Base(view[0]), a_);
  EXPECT_EQ(view[0]->iov_len, sizeof(a_));
}

TEST_F(IoVecViewTest, AdvanceWithinIoVec) {
  IoVecView view(iovs_);
  // A first partial advance.
  EXPECT_EQ(view.Advance(1), 0);
  EXPECT_EQ(Base(view[0]), a_ + 1);
  EXPECT_EQ(view[0]->iov_len, 6);

  // A second partial advance accumulates on the same iovec.
  EXPECT_EQ(view.Advance(2), 0);
  EXPECT_EQ(Base(view[0]), a_ + 3);
  EXPECT_EQ(view[0]->iov_len, 4);
  EXPECT_EQ(view.Advance(3), 0);
  EXPECT_EQ(Base(view[0]), a_ + 6);
  EXPECT_EQ(view[0]->iov_len, 1);
  EXPECT_EQ(view.Advance(1), 1);
  EXPECT_EQ(Base(view[1]), b_ + 0);
  EXPECT_EQ(view[0]->iov_len, 1);

  // Later iovecs are untouched.
  EXPECT_EQ(Base(view[1]), b_);
  EXPECT_EQ(view[1]->iov_len, sizeof(b_));
}

TEST_F(IoVecViewTest, AdvanceToBoundary) {
  IoVecView view(iovs_);
  EXPECT_EQ(view.Advance(sizeof(a_)), 1);
  EXPECT_EQ(Base(view[1]), b_);
  EXPECT_EQ(view[1]->iov_len, sizeof(b_));
}

TEST_F(IoVecViewTest, AdvanceAcrossIoVecs) {
  IoVecView view(iovs_);
  EXPECT_EQ(view.Advance(sizeof(a_) + sizeof(b_) + 2), 2);
  EXPECT_EQ(Base(view[2]), c_ + 2);
  EXPECT_EQ(view[2]->iov_len, 7);
}

TEST_F(IoVecViewTest, AdvanceFromMiddleAcrossBoundary) {
  IoVecView view(iovs_);
  ASSERT_EQ(view.Advance(2), 0);
  EXPECT_EQ(view.Advance(5 + 3), 1);
  EXPECT_EQ(Base(view[1]), b_ + 3);
  EXPECT_EQ(view[1]->iov_len, 5);
}

TEST_F(IoVecViewTest, AdvanceOneByteAtATime) {
  IoVecView view(iovs_);
  for (size_t done = 1; done <= kTotal; ++done) {
    const int want = (done >= 7) + (done >= 15) + (done >= 24);
    EXPECT_EQ(view.Advance(1), want) << "after " << done << " bytes";
  }
}

TEST_F(IoVecViewTest, AdvanceToAndBeyondEnd) {
  IoVecView view(iovs_);
  EXPECT_EQ(view.Advance(kTotal), 3);
  // Further advances stay at the end.
  EXPECT_EQ(view.Advance(0), 3);
  EXPECT_EQ(view.Advance(1), 3);

  IoVecView view2(iovs_);
  EXPECT_EQ(view2.Advance(1000), 3);
  EXPECT_EQ(view2.size(), 3);
}

TEST_F(IoVecViewTest, DoesNotModifyCallerIoVecs) {
  IoVecView view(iovs_);
  ASSERT_EQ(view.Advance(7), 1);
  EXPECT_EQ(iovs_[0].iov_base, a_);
  EXPECT_EQ(iovs_[1].iov_base, b_);
  EXPECT_EQ(iovs_[0].iov_len, sizeof(a_));
  EXPECT_EQ(iovs_[1].iov_len, sizeof(b_));
}

TEST_F(IoVecViewTest, CallerArrayNeedNotOutliveView) {
  auto iovs = std::make_unique<std::array<IoVec, 3>>(iovs_);
  IoVecView view(*iovs);
  iovs.reset();
  EXPECT_EQ(view.Advance(8), 1);
  EXPECT_EQ(Base(view[1]), b_ + 1);
  EXPECT_EQ(view[1]->iov_len, 7);
}

}  // namespace
}  // namespace peregrine::internal::testing
