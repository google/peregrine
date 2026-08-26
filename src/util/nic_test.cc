#include "src/util/nic.h"

#include <string>
#include <string_view>
#include <vector>

#include "gtest/gtest.h"
#include "absl/container/flat_hash_map.h"
#include "absl/log/log.h"

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

void PrintIps(
    std::string_view title,
    absl::flat_hash_map<std::string, std::vector<std::string>> nic_ips) {
  LOG(INFO) << title;
  for (const auto& [nic, ips] : nic_ips) {
    LOG(INFO) << " " << nic;
    for (const std::string& ip : ips) {
      LOG(INFO) << "  " << ip;
    }
  }
}

TEST(NicsTest, EnumerateNics) { PrintNics(); }

TEST(NicsTest, FindRoutableIpAddrs) {
  PrintIps("IPv4/v6", FindRoutableIpAddrs(AF_UNSPEC));
  PrintIps("IPv4", FindRoutableIpAddrs(AF_INET));
  PrintIps("IPv6", FindRoutableIpAddrs(AF_INET6));
}

}  // namespace
}  // namespace peregrine::util::testing
