#include "src/internal/util/test_param.h"

#include <sys/socket.h>

#include <string>

#include "absl/strings/str_format.h"

namespace peregrine::internal::testing {

std::string ToString(const TestChannelType t) {
  switch (t) {
    case TestChannelType::kTcp:
      return "TcpChannel";
    case TestChannelType::kUdp:
      return "UdpChannel";
    case TestChannelType::kMemStream:
      return "MemStreamChannel";
    case TestChannelType::kMemMsg:
      return "MemMessageChannel";
  }
}

std::string FamilyToString(const int family) {
  if (family == AF_INET) return "IPv4";
  if (family == AF_INET6) return "IPv6";
  return "";
}

std::string ToString(const TestChannelErrorParam& param) {
  return absl::StrFormat("%s_%s_ErrorRate_%d", ToString(param.type),
                         FamilyToString(param.family), param.error_rate);
}

std::string ToString(const TestChannelSizeParam& param) {
  return absl::StrFormat("%s_%s_Size_%zu", ToString(param.type),
                         FamilyToString(param.family), param.size);
}

}  // namespace peregrine::internal::testing
