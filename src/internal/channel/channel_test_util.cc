#include "src/internal/channel/channel_test_util.h"

#include <sys/socket.h>

#include <memory>
#include <string>
#include <utility>

#include "absl/log/check.h"
#include "absl/strings/str_cat.h"
#include "src/internal/channel/channel_msg.h"
#include "src/internal/channel/channel_stream.h"
#include "src/internal/channel/channel_util.h"
#include "src/internal/channel/pipe.h"
#include "src/internal/socket/socket_test_util.h"

namespace peregrine::internal::testing {

std::string TestChannelToString(const TestChannelType type,
                                const int error_rate) {
  switch (type) {
    case TestChannelType::kTcp:
      return "Tcp";
    case TestChannelType::kUdp:
      return "Udp";
    case TestChannelType::kMemStream:
      return "MemStream";
    case TestChannelType::kMemMsg:
      return absl::StrCat("MemMsg_ER", error_rate);
  }
  return "unknown";
}

ConnectedChannelPair CreateTestChannelPair(const TestChannelType type,
                                           const int error_rate) {
  switch (type) {
    case TestChannelType::kTcp:
      DCHECK_EQ(error_rate, 0);
      return ConnectedChannelPair::CreateTcp(AF_INET);
    case TestChannelType::kUdp:
      DCHECK_EQ(error_rate, 0);
      return ConnectedChannelPair::CreateUdp(AF_INET);
    case TestChannelType::kMemStream:
      return ConnectedChannelPair::CreateMemStream();
    case TestChannelType::kMemMsg:
      return ConnectedChannelPair::CreateMemMsg(error_rate);
  }
  DCHECK(false);
}

ConnectedChannelPair ConnectedChannelPair::CreateTcp(const int family) {
  auto [sa, sb] = CreateTcpSocketPair(family);
  DCHECK_NE(sa, nullptr);
  DCHECK_NE(sb, nullptr);
  return {CreateTcpChannel(std::move(sa)), CreateTcpChannel(std::move(sb))};
}

ConnectedChannelPair ConnectedChannelPair::CreateUdp(const int family) {
  auto [sa, sb] = CreateUdpSocketPair(family);
  DCHECK_NE(sa, nullptr);
  DCHECK_NE(sb, nullptr);
  return {CreateUdpChannel(std::move(sa)), CreateUdpChannel(std::move(sb))};
}

ConnectedChannelPair ConnectedChannelPair::CreateMemStream() {
  auto [pipe_a, pipe_b] = BidiPipe::Create();
  auto a = std::make_unique<MemStreamChannel>(pipe_a);
  auto b = std::make_unique<MemStreamChannel>(pipe_b);
  return {std::move(a), std::move(b)};
}

ConnectedChannelPair ConnectedChannelPair::CreateMemMsg(const int error_rate) {
  auto [pipe_a, pipe_b] = BidiPipe::Create();
  auto a = std::make_unique<MemMsgChannel>(pipe_a, error_rate);
  auto b = std::make_unique<MemMsgChannel>(pipe_b, error_rate);
  return {std::move(a), std::move(b)};
}

}  // namespace peregrine::internal::testing
