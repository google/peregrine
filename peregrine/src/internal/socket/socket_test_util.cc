#include "peregrine/src/internal/socket/socket_test_util.h"

#include <cstddef>
#include <memory>
#include <utility>

#include "absl/log/check.h"
#include "absl/random/random.h"
#include "absl/synchronization/notification.h"
#include "peregrine/src/internal/base/endpoint.h"
#include "peregrine/src/internal/base/hostinfo.h"
#include "peregrine/src/internal/socket/socket_tcp.h"
#include "peregrine/src/internal/socket/socket_udp.h"
#include "peregrine/src/internal/socket/tcp_manager.h"
#include "peregrine/src/internal/util/test_util.h"
#include "peregrine/src/util/nic.h"
#include "peregrine/src/util/thread.h"

namespace peregrine::internal::testing {

namespace {
Endpoint PickPeer(const HostInfo& peer_host) {
  DCHECK(peer_host.IsValid());
  for (const auto& nic : peer_host.data_plane_listeners) {
    if (nic.type != util::NicType::kIP) continue;
    const size_t n = nic.endpoints.size();
    CHECK_GE(n, 1);
    if (n == 1) return nic.endpoints[0];
    absl::BitGen bitgen;
    return nic.endpoints[absl::Uniform<size_t>(bitgen, 0, n)];
  }
  CHECK(false) << "no peer tcp endpoint found in " << peer_host;  // Crash OK
}
}  // namespace

std::pair<std::unique_ptr<TcpSocket>, std::unique_ptr<TcpSocket>>
CreateTcpSocketPair(int family, bool blocking) {
  HostInfo a(TestOnly_LocalHostInfo(family, /*tcp=*/true));
  std::unique_ptr<TcpManager> mgr = TcpManager::Create(a);
  CHECK_NE(mgr, nullptr);

  std::unique_ptr<TcpSocket> sa = nullptr;
  absl::Notification mgr_started;
  absl::Notification socket_accepted;
  auto accept = [&](std::unique_ptr<TcpSocket> socket) {
    sa = std::move(socket);
    socket_accepted.Notify();
  };
  util::Thread mgr_thread([&]() {
    mgr_started.Notify();
    mgr->Start(accept, blocking);
  });

  mgr_started.WaitForNotification();
  const Endpoint self = {};
  const Endpoint peer = PickPeer(a);
  std::unique_ptr<TcpSocket> sb = TcpManager::Connect(self, peer);

  socket_accepted.WaitForNotification();
  mgr->Stop();
  mgr_thread.join();

  CHECK_NE(sa, nullptr);
  CHECK_NE(sb, nullptr);
  return {std::move(sa), std::move(sb)};
}

std::pair<std::unique_ptr<UdpSocket>, std::unique_ptr<UdpSocket>>
CreateUdpSocketPair(int family, bool blocking) {
  const Endpoint a(TestOnly_LocalEndpoint(family, /*tcp=*/false));
  const Endpoint b(TestOnly_LocalEndpoint(family, /*tcp=*/false));

  std::unique_ptr<UdpSocket> sa = TestOnly_CreateUdpSocket(family, blocking);
  std::unique_ptr<UdpSocket> sb = TestOnly_CreateUdpSocket(family, blocking);

  CHECK(!sa->Bind(a));
  CHECK(!sb->Bind(b));
  CHECK(!sa->Connect(b));
  CHECK(!sb->Connect(a));

  return {std::move(sa), std::move(sb)};
}

}  // namespace peregrine::internal::testing
