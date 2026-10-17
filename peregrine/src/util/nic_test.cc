#include "peregrine/src/util/nic.h"

#include <string>
#include <string_view>

#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "absl/container/flat_hash_map.h"
#include "absl/log/log.h"
#include "absl/status/status.h"
#include "absl/status/status_matchers.h"
#include "peregrine/src/util/ipaddr.h"

namespace peregrine::util::testing {
namespace {

using ::absl::StatusCode::kNotFound;
using ::absl_testing::IsOkAndHolds;
using ::absl_testing::StatusIs;
using ::testing::Ge;

void PrintNics() {
  LOG(INFO) << "NICs";
  for (const auto& [nic, ips] : EnumerateNics()) {
    LOG(INFO) << " " << nic;
    for (const std::string& ip : ips) {
      LOG(INFO) << "  " << ip;
    }
  }
}

void PrintIps(std::string_view title,
              const absl::flat_hash_map<std::string, NicInfo>& nic_infos) {
  LOG(INFO) << title;
  for (const auto& [nic, ni] : nic_infos) {
    LOG(INFO) << " " << nic;
    for (const IpAddr& addr : ni.addrs) {
      LOG(INFO) << "  " << ni.type << ", " << addr;
    }
  }
}

TEST(NicsTest, EnumerateNics) { PrintNics(); }

TEST(NicsTest, FindNic) {
  const IpAddr ip = *IpAddr::Create("127.0.0.1");
  EXPECT_THAT(FindNic(ip), IsOkAndHolds(Ge(1)));
}

TEST(NicsTest, FindNicMissing) {
  const IpAddr ip = *IpAddr::Create("192.0.2.1");
  EXPECT_THAT(FindNic(ip), StatusIs(kNotFound));
}

TEST(NicsTest, FindRoutableIpAddrs) {
  PrintIps("IPv4/v6", FindRoutableIpAddrs(AF_UNSPEC));
  PrintIps("IPv4", FindRoutableIpAddrs(AF_INET));
  PrintIps("IPv6", FindRoutableIpAddrs(AF_INET6));
}

TEST(NicsTest, ToString) {
  const IpAddr ip1 = *IpAddr::Create("192.168.1.1");
  const IpAddr ip2 = *IpAddr::Create("10.0.0.1");
  const NicInfo ni{
      .type = NicType::kIP,
      .addrs = {ip1, ip2},
  };
  LOG(INFO) << ni;
}

}  // namespace
}  // namespace peregrine::util::testing
