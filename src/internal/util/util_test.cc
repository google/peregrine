#include "src/internal/util/util.h"

#include "gtest/gtest.h"
#include "absl/log/log.h"

namespace peregrine::internal::testing {
namespace {

TEST(UtilTest, IsPowerOfTwo) {
  EXPECT_FALSE(IsPowerOfTwo(0));
  EXPECT_TRUE(IsPowerOfTwo(1));
  EXPECT_TRUE(IsPowerOfTwo(2));
  EXPECT_FALSE(IsPowerOfTwo(3));
  EXPECT_TRUE(IsPowerOfTwo(4));
  EXPECT_FALSE(IsPowerOfTwo(15));
  EXPECT_TRUE(IsPowerOfTwo(16));
}

TEST(UtilTest, TotalLength) {
  const struct iovec iov[] = {
      {.iov_base = (void*)0xffff0000, .iov_len = 1024},
      {.iov_base = (void*)0xffff1000, .iov_len = 2048},
      {.iov_base = (void*)0xffff2000, .iov_len = 4096},
  };
  EXPECT_EQ(TotalLength(iov, 3), 7168);
}

TEST(UtilTest, ThreadId) {
  EXPECT_EQ(ThreadId(), ThreadId());
  LOG(INFO) << "ThreadId: " << ThreadId();
}

}  // namespace
}  // namespace peregrine::internal::testing
