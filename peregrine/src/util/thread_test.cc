#include "peregrine/src/util/thread.h"

#include "gtest/gtest.h"
#include "absl/log/log.h"

namespace peregrine::util::testing {
namespace {

TEST(ThreadTest, ThreadId) {
  EXPECT_EQ(ThreadId(), ThreadId());
  LOG(INFO) << "thread #" << ThreadId();
}

}  // namespace
}  // namespace peregrine::util::testing
