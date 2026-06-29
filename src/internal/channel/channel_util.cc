#include "src/internal/channel/channel_util.h"

#include <memory>
#include <utility>

#include "absl/log/check.h"
#include "src/internal/base/endpoint.h"
#include "src/internal/channel/channel.h"
#include "src/internal/socket/connector.h"
#include "src/internal/socket/socket_tcp.h"

namespace peregrine::internal {

Channels Create(const Endpoint& peer, int num_channels) {
  Channels chs;
  chs.reserve(num_channels);
  for (int i = 0; i < 2 * num_channels; ++i) {
    std::unique_ptr<TcpSocket> socket = TcpConnector::Create(peer);
    if (socket == nullptr) continue;
    DCHECK(socket->IsBlocking());

    std::unique_ptr<Channel> ch = CreateTcpChannel(std::move(socket));
    DCHECK_NE(ch, nullptr);

    chs.emplace_back(std::move(ch));
    if (chs.size() >= num_channels) break;
  }
  return chs;
}

}  // namespace peregrine::internal
