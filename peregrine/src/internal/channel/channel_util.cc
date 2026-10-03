#include "peregrine/src/internal/channel/channel_util.h"

#include <memory>
#include <utility>
#include <vector>

#include "absl/log/check.h"
#include "absl/time/clock.h"
#include "absl/time/time.h"
#include "peregrine/src/internal/assumptions.h"
#include "peregrine/src/internal/base/endpoint.h"
#include "peregrine/src/internal/channel/channel.h"
#include "peregrine/src/internal/socket/socket_tcp.h"
#include "peregrine/src/internal/socket/tcp_manager.h"

namespace peregrine::internal {

std::vector<std::unique_ptr<Channel>> Create(TcpManager& tcp_mgr,
                                             const Endpoint& self,
                                             const Endpoint& peer,
                                             const bool blocking, const int n) {
  static_assert(assumptions::kAllConnectedTcpSocketsAreStillBlocking);
  DCHECK(peer.HasNonzeroIpPort());
  DCHECK_GE(n, 1);

  std::vector<std::unique_ptr<Channel>> chs;
  chs.reserve(n);
  int pending = 0;
  int attempts = 0;
  const absl::Time deadline = absl::Now() + absl::Seconds(10);
  while (chs.size() < n && absl::Now() < deadline) {
    while (chs.size() + pending < n && attempts < 2 * n) {
      ++attempts;
      if (tcp_mgr.Connect(self, peer, blocking) >= 0) {
        ++pending;
      }
    }
    for (auto& socket : tcp_mgr.GetOutgoingSockets()) {
      --pending;
      DCHECK(socket->MatchesBlocking(blocking));
      if (!blocking && socket->SetBlocking() < 0) continue;
      std::unique_ptr<Channel> ch = CreateTcpChannel(std::move(socket));
      DCHECK_NE(ch, nullptr);
      chs.emplace_back(std::move(ch));
      if (chs.size() >= n) return chs;
    }
    if (pending == 0 && attempts >= 2 * n) break;
    if (!blocking) absl::SleepFor(absl::Milliseconds(1));
  }
  return chs;
}

}  // namespace peregrine::internal
