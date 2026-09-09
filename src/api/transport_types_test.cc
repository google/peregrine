#include "src/api/transport_types.h"

#include <cstddef>

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

TEST(TransportType, Values) {
  EXPECT_EQ(static_cast<int>(TransportType::kTcp), 1);
  EXPECT_EQ(static_cast<int>(TransportType::kRdma), 2);
}

TEST(TransportStatus, Values) {
  EXPECT_FALSE(IsInProgress(Status::kNotFound));
  EXPECT_FALSE(IsCompleted(Status::kNotFound));
  LOG(INFO) << "Status: " << Status::kNotFound;

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
  EXPECT_FALSE(IsValid({}));

  const Request ri = {
      .op = Op::kRead,
      .laddr = nullptr,
      .raddr = nullptr,
      .len = 0,
  };
  EXPECT_FALSE(ri.IsValid());
  EXPECT_FALSE(IsValid({ri}));
  LOG(INFO) << ri;

  const Request rv = {
      .op = Op::kRead,
      .laddr = reinterpret_cast<Byte*>(0x1234),
      .raddr = reinterpret_cast<Byte*>(0xabcd),
      .len = 1,
  };
  EXPECT_TRUE(rv.IsValid());
  EXPECT_TRUE(IsValid({rv}));
  LOG(INFO) << rv;
}

TEST(TransportRequest, RKeyAndEquality) {
  const Request r1 = {
      .op = Op::kRead,
      .laddr = reinterpret_cast<Byte*>(0x1000),
      .raddr = reinterpret_cast<Byte*>(0x2000),
      .len = 1024,
      .rkey = 0xABCD,
  };
  const Request r2 = {
      .op = Op::kRead,
      .laddr = reinterpret_cast<Byte*>(0x1000),
      .raddr = reinterpret_cast<Byte*>(0x2000),
      .len = 1024,
      .rkey = 0xABCD,
  };
  const Request r3 = {
      .op = Op::kRead,
      .laddr = reinterpret_cast<Byte*>(0x1000),
      .raddr = reinterpret_cast<Byte*>(0x2000),
      .len = 1024,
      .rkey = 0x1234,
  };

  EXPECT_TRUE(r1.IsValid());
  EXPECT_EQ(r1, r2);
  EXPECT_NE(r1, r3);
  EXPECT_NE(r1.ToString().find("rkey: 0xabcd"), std::string::npos);
}

TEST(TransportMetrics, DefaultInitialization) {
  const TransportMetrics m;

  // Verify every byte in the struct is zeroed without naming any fields.
  const auto* bytes = reinterpret_cast<const unsigned char*>(&m);
  for (size_t i = 0; i < sizeof(TransportMetrics); ++i) {
    EXPECT_EQ(bytes[i], 0);
  }
}

}  // namespace
}  // namespace peregrine::testing
