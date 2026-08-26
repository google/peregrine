#include "src/util/nic.h"

#include <string>
#include <string_view>

#include "gtest/gtest.h"
#include "absl/container/flat_hash_map.h"
#include "absl/log/log.h"
#include "src/util/ipaddr.h"

namespace peregrine::util::testing {
namespace {

void PrintNics() {
  LOG(INFO) << "NICs";
  for (const auto& [nic, ips] : EnumerateNics()) {
    LOG(INFO) << " " << nic << "";
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
    for (const util::IpAddr& addr : ni.addrs) {
      LOG(INFO) << "  " << ni.type << ", " << addr;
    }
  }
}

TEST(NicsTest, EnumerateNics) { PrintNics(); }

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
