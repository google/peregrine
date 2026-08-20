#include "src/internal/base/hostinfo.h"

#include "gtest/gtest.h"
#include "absl/log/log.h"
#include "src/internal/base/endpoint.h"

namespace peregrine::internal::testing {
namespace {

TEST(HostInfoTest, IsValid) {
  const Endpoint c = Endpoint::Create("0.0.0.0:10000");
  const Endpoint d0 = Endpoint::Create("10.0.0.1:35247");
  const Endpoint d1 = Endpoint::Create("10.0.0.2:51691");
  ASSERT_FALSE(c.HasZeroPort());
  ASSERT_TRUE(d0.HasNonzeroIpPort());
  ASSERT_TRUE(d1.HasNonzeroIpPort());

  const HostInfo host = {.control_plane_listener = c,
                         .data_plane_listeners = {d0, d1}};
  EXPECT_TRUE(host.IsValid());
  LOG(INFO) << host;
}

TEST(HostInfoTest, Create) {
  const HostInfo a = HostInfo::Create("127.0.0.1:12345");
  EXPECT_TRUE(a.IsValid());
  LOG(INFO) << a;

  const HostInfo b = HostInfo::Create("127.0.0.1:12345, [::1]:54321");
  EXPECT_TRUE(b.IsValid());
  LOG(INFO) << b;
}

TEST(HostInfoTest, ToString) {
  const Endpoint c = Endpoint::Create("127.0.0.1:12345");
  const Endpoint d = Endpoint::Create("[::1]:54321");
  const HostInfo host = {.control_plane_listener = c,
                         .data_plane_listeners = {d}};
  EXPECT_TRUE(host.IsValid());
  EXPECT_EQ(host.ToString(), "host: 127.0.0.1:12345, [::1]:54321");
  LOG(INFO) << host;
}

}  // namespace
}  // namespace peregrine::internal::testing
