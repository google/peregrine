#include "peregrine/src/internal/util/test_param.h"

#include <sys/socket.h>

#include <string>

#include "absl/strings/str_format.h"

namespace peregrine::internal::testing {

std::string ChannelTypeToString(const TestChannelType t) {
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
  return "FamilyUnknown";
}

std::string BlockingToString(const bool blocking) {
  return blocking ? "Blocking" : "NonBlocking";
}

std::string SocketTestConfig::ToString() const {
  return absl::StrFormat("%s_%sSocket", FamilyToString(family),
                         BlockingToString(blocking));
}

std::string ChannelTestParam::ToString() const {
  return absl::StrFormat("%s_%s_ErrorRate_%d_Size_%u",
                         ChannelTypeToString(type), FamilyToString(family),
                         error_rate, size);
}

}  // namespace peregrine::internal::testing
