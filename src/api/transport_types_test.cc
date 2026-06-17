#include "src/api/transport_types.h"

#include "gtest/gtest.h"
#include "absl/log/log.h"

namespace peregrine::testing {
namespace {

TEST(TransportOp, ToString) {
  EXPECT_EQ(static_cast<int>(Op::kRead), 1);
  EXPECT_EQ(ToString(Op::kRead), "Read");
  LOG(INFO) << "Op: " << Op::kRead;

  EXPECT_EQ(static_cast<int>(Op::kWrite), 2);
  EXPECT_EQ(ToString(Op::kWrite), "Write");
  LOG(INFO) << "Op: " << Op::kWrite;
}

TEST(TransportStatus, Values) {
  EXPECT_TRUE(IsInProgress(Status::kInProgress));
  EXPECT_FALSE(IsCompleted(Status::kInProgress));
  LOG(INFO) << "Status: " << Status::kInProgress;

  EXPECT_TRUE(IsCompleted(Status::kSuccess));
  EXPECT_FALSE(IsInProgress(Status::kSuccess));
  LOG(INFO) << "Status: " << Status::kSuccess;

  EXPECT_TRUE(IsCompleted(Status::kFailure));
  EXPECT_FALSE(IsInProgress(Status::kFailure));
  LOG(INFO) << "Status: " << Status::kFailure;
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
