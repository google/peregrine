#include "peregrine/src/internal/channel/channel_util.h"

#include <memory>
#include <utility>
#include <vector>

#include "absl/log/check.h"
#include "peregrine/src/internal/base/endpoint.h"
#include "peregrine/src/internal/channel/channel.h"
#include "peregrine/src/internal/socket/socket_tcp.h"
#include "peregrine/src/internal/socket/tcp_manager.h"

namespace peregrine::internal {

std::vector<std::unique_ptr<Channel>> Create(TcpManager& tcp_mgr,
                                             const Endpoint& self,
                                             const Endpoint& peer,
                                             const bool blocking, const int n) {
  DCHECK(peer.HasNonzeroIpPort());
  DCHECK_GE(n, 1);

  std::vector<std::unique_ptr<Channel>> chs;
  chs.reserve(n);
  for (int i = 0; i < 2 * n; ++i) {
    tcp_mgr.Connect(self, peer, blocking);
    for (auto& socket : tcp_mgr.GetConnected()) {
      DCHECK_EQ(socket->IsBlocking(), blocking);
      std::unique_ptr<Channel> ch = CreateTcpChannel(std::move(socket));
      DCHECK_NE(ch, nullptr);
      chs.emplace_back(std::move(ch));
      if (chs.size() >= n) return chs;
    }
  }
  return chs;
}

}  // namespace peregrine::internal
