#include "src/internal/base/endpoint.h"

#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "absl/log/log.h"
#include "absl/status/status.h"

namespace peregrine::testing {
namespace {

using ::testing::status::IsOkAndHolds;
using ::testing::status::StatusIs;

TEST(EndpointTest, Create) {
  EXPECT_THAT(Endpoint::Create("127.0.0.1:12345"),
              IsOkAndHolds(Endpoint("127.0.0.1", 12345)));
  EXPECT_THAT(Endpoint::Create("[::1]:12345"),
              IsOkAndHolds(Endpoint("::1", 12345)));

  EXPECT_THAT(Endpoint::Create("?"),
              StatusIs(absl::StatusCode::kInvalidArgument));
  EXPECT_THAT(Endpoint::Create("127.0.0.1"),
              StatusIs(absl::StatusCode::kInvalidArgument));
  EXPECT_THAT(Endpoint::Create("::1:"),
              StatusIs(absl::StatusCode::kInvalidArgument));
}

TEST(EndpointTest, Basic) {
  const Endpoint a("127.0.0.1", 9999);
  const Endpoint b{"127.0.0.1", 9999};
  const Endpoint c("127.0.0.2", 9999);
  const Endpoint d("127.0.0.1", 7777);
  EXPECT_EQ(a, b);
  EXPECT_NE(a, c);
  EXPECT_NE(a, d);

  EXPECT_EQ(a.Hash(), b.Hash());
  EXPECT_EQ(Endpoint::Hash(a), Endpoint::Hash(b));

  EXPECT_TRUE(a.IsValid());
  LOG(INFO) << "endpoint = " << a;

  EXPECT_FALSE(Endpoint("?", 9999).IsValid());
  EXPECT_FALSE(Endpoint("127.0.0.1", 0).IsValid());
}

}  // namespace
}  // namespace peregrine::testing
