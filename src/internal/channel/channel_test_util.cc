#include "src/internal/channel/channel_test_util.h"

#include <sys/socket.h>

#include <memory>
#include <utility>

#include "absl/log/check.h"
#include "src/internal/channel/channel_msg.h"
#include "src/internal/channel/channel_stream.h"
#include "src/internal/channel/channel_util.h"
#include "src/internal/channel/pipe.h"
#include "src/internal/socket/socket_test_util.h"

namespace peregrine::internal::testing {

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
