#include "src/util/interface.h"

#include <sys/socket.h>

#include <string>

#include "gtest/gtest.h"
#include "absl/log/log.h"
#include "src/util/nic.h"

namespace peregrine::util {
namespace {

// TODO: For comprehensive testing, add helper functions to mock network
// interfaces and sysfs filesystem paths.

// Logs host network topology and enumerated IP interfaces. No assertions.
TEST(InterfaceTest, EnumerateIpInterfaces_HostTopology) {
  LOG(INFO) << "Host Nics:";
  for (const auto& [nic, ips] : EnumerateNics()) {
    LOG(INFO) << "  NIC " << nic << ":";
    for (const std::string& ip : ips) {
      LOG(INFO) << "    IP: " << ip;
    }
  }

  LOG(INFO) << "EnumerateIpInterfaces (All):";
  for (const auto& ip : EnumerateIpInterfaces(AF_UNSPEC)) {
    LOG(INFO) << "  " << ip;
  }

  LOG(INFO) << "EnumerateIpInterfaces (IPv4):";
  for (const auto& ip : EnumerateIpInterfaces(AF_INET)) {
    LOG(INFO) << "  " << ip;
  }

  LOG(INFO) << "EnumerateIpInterfaces (IPv6):";
  for (const auto& ip : EnumerateIpInterfaces(AF_INET6)) {
    LOG(INFO) << "  " << ip;
  }
}

}  // namespace
}  // namespace peregrine::util
