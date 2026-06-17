#include "gtest/gtest.h"
#include "src/api/transport_types.h"
#include "src/internal/control/message.pb.h"

namespace peregrine::internal::testing {
namespace {

TEST(MessageTest, RequestOp) {
  EXPECT_EQ(static_cast<int>(proto::Request::INVALID), 0);

  EXPECT_EQ(static_cast<int>(proto::Request::READ),
            static_cast<int>(Op::kRead));

  EXPECT_EQ(static_cast<int>(proto::Request::WRITE),
            static_cast<int>(Op::kWrite));
}

}  // namespace
}  // namespace peregrine::internal::testing
