#include "src/internal/base/endpoint.h"

#include <utility>

#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "absl/container/flat_hash_set.h"
#include "absl/log/log.h"
#include "absl/status/status.h"
#include "src/internal/base/types.h"

namespace peregrine::internal::testing {
namespace {

using ::absl::StatusCode::kInvalidArgument;
using ::testing::status::IsOkAndHolds;
using ::testing::status::StatusIs;

constexpr ipv4_t kIPv4{.s_addr = 0x0100007f};
constexpr ipv6_t kIPv6 = IN6ADDR_LOOPBACK_INIT;

TEST(EndpointTest, Create) {
  EXPECT_THAT(Endpoint::Create("127.0.0.1:12345"),
              IsOkAndHolds(Endpoint(kIPv4, 12345)));
  EXPECT_THAT(Endpoint::Create("[::1]:54321"),
              IsOkAndHolds(Endpoint(kIPv6, 54321)));

  EXPECT_THAT(Endpoint::Create("?"), StatusIs(kInvalidArgument));
  EXPECT_THAT(Endpoint::Create("::1"), StatusIs(kInvalidArgument));
  EXPECT_THAT(Endpoint::Create("127.0.0.1:"), StatusIs(kInvalidArgument));
}

TEST(EndpointTest, Validity) {
  EXPECT_FALSE(Endpoint().IsValid());
  EXPECT_FALSE(Endpoint(kIPv4, 0).IsValid());

  const Endpoint a(kIPv4, 12345);
  EXPECT_TRUE(a.IsValid());
  LOG(INFO) << "endpoint = " << a;
}

TEST(EndpointTest, Ctors) {
  const Endpoint a(kIPv4, 9999);
  const Endpoint b(a);
  const Endpoint c = a;
  LOG(INFO) << "a = " << a;
  LOG(INFO) << "b = " << b;
  LOG(INFO) << "c = " << c;

  const Endpoint d(std::move(a));
  const Endpoint e = std::move(b);
  LOG(INFO) << "d = " << d;
  LOG(INFO) << "e = " << e;
}

TEST(EndpointTest, Hash) {
  const Endpoint a(kIPv4, 9999);
  const Endpoint b(kIPv4, 9999);
  const Endpoint c(kIPv6, 9999);
  const Endpoint d(kIPv4, 7777);
  EXPECT_EQ(a, b);
  EXPECT_NE(a, c);
  EXPECT_NE(a, d);

  EXPECT_EQ(a.Hash(), b.Hash());
  EXPECT_EQ(Endpoint::Hash(a), Endpoint::Hash(b));

  absl::flat_hash_set<Endpoint> set;
  set.insert(a);
  set.insert(b);
  set.insert(c);
  set.insert(d);
  EXPECT_EQ(set.size(), 3);
}

}  // namespace
}  // namespace peregrine::internal::testing
