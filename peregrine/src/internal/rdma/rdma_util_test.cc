#include "peregrine/src/internal/rdma/rdma_util.h"

#include <cstdint>
#include <cstring>
#include <optional>

#include "gtest/gtest.h"
#include "peregrine/src/util/ipaddr.h"

namespace peregrine::internal::testing {
namespace {

using ::peregrine::util::IpAddr;

TEST(RdmaUtilTest, IpAddrToRdmaGid) {
  // IPv4
  EXPECT_FALSE(IpAddrToRdmaGid(*IpAddr::Create("0.0.0.0")).has_value());

  // IPv6
  const auto ip6 = IpAddr::Create("2002:a05:7538:2502::1");
  const uint8_t expected_ip6_gid[] = {0x20, 0x02, 0x0a, 0x05, 0x75, 0x38,
                                      0x25, 0x02, 0x00, 0x00, 0x00, 0x00,
                                      0x00, 0x00, 0x00, 0x01};
  const auto ip6_gid = IpAddrToRdmaGid(*ip6);
  ASSERT_TRUE(ip6_gid.has_value());
  EXPECT_EQ(std::memcmp(ip6_gid.value().raw, expected_ip6_gid, 16), 0);

  // IPv4-mapped IPv6
  const auto ip4m = IpAddr::Create("::ffff:127.0.0.1");
  const uint8_t expected_ip4m_gid[] = {0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
                                       0x00, 0x00, 0x00, 0x00, 0xff, 0xff,
                                       0x7f, 0x00, 0x00, 0x01};
  const auto ip4m_gid = IpAddrToRdmaGid(*ip4m);
  ASSERT_TRUE(ip4m_gid.has_value());
  EXPECT_EQ(std::memcmp(ip4m_gid.value().raw, expected_ip4m_gid, 16), 0);
}

}  // namespace
}  // namespace peregrine::internal::testing
