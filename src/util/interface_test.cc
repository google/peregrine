#include "src/util/interface.h"

#include <sys/socket.h>

#include <string>
#include <string_view>

#include "gtest/gtest.h"
#include "absl/log/log.h"
#include "absl/types/span.h"
#include "src/util/nic.h"

namespace peregrine::util {
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

void PrintIps(std::string_view title, absl::Span<const std::string> ips) {
  LOG(INFO) << title;
  for (const auto& ip : ips) {
    LOG(INFO) << "  " << ip;
  }
}

TEST(InterfaceTest, FindRoutableIpAddrs) {
  PrintNics();
  PrintIps("IPv4/v6", FindRoutableIpAddrs(AF_UNSPEC));
  PrintIps("IPv4", FindRoutableIpAddrs(AF_INET));
  PrintIps("IPv6", FindRoutableIpAddrs(AF_INET6));
}

}  // namespace
}  // namespace peregrine::util
