#include "src/api/types.h"

#include "gtest/gtest.h"
#include "absl/log/log.h"

namespace peregrine::testing {
namespace {

TEST(TransportOp, ToString) {
  EXPECT_EQ(ToString(Op::kRead), "Read");
  EXPECT_EQ(ToString(Op::kWrite), "Write");
}

TEST(TransportStatus, Values) {
  EXPECT_TRUE(IsInProgress(Status::kInProgress));
  EXPECT_FALSE(IsInProgress(Status::kSuccess));
  EXPECT_FALSE(IsInProgress(Status::kFailure));

  EXPECT_FALSE(IsCompleted(Status::kInProgress));
  EXPECT_TRUE(IsCompleted(Status::kSuccess));
  EXPECT_TRUE(IsCompleted(Status::kFailure));
}

TEST(TransportRequest, Validity) {
  const Request ri = {
      .op = Op::kRead,
      .laddr = nullptr,
      .raddr = nullptr,
      .len = 0,
  };
  EXPECT_FALSE(ri.IsValid());
  LOG(INFO) << ri;

  const Request rv = {
      .op = Op::kRead,
      .laddr = reinterpret_cast<Byte*>(0x1234),
      .raddr = reinterpret_cast<Byte*>(0xabcd),
      .len = 1,
  };
  EXPECT_TRUE(rv.IsValid());
  LOG(INFO) << rv;
}

}  // namespace
}  // namespace peregrine::testing
