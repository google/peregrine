#include "peregrine/src/internal/socket/socket_test_util.h"

#include <cstddef>
#include <memory>
#include <utility>
#include <vector>

#include "absl/log/check.h"
#include "absl/random/random.h"
#include "absl/synchronization/notification.h"
#include "absl/time/clock.h"
#include "absl/time/time.h"
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

std::unique_ptr<TcpSocket> GetOneSocket(TcpManager& mgr, bool accepted) {
  auto sockets = accepted ? mgr.GetIncomingSockets() : mgr.GetOutgoingSockets();
  if (sockets.empty()) return nullptr;
  CHECK_EQ(sockets.size(), 1);
  return std::move(sockets.front());
}
}  // namespace

std::unique_ptr<TcpSocket> GetOneIncomingSocket(TcpManager& mgr) {
  return GetOneSocket(mgr, /*accepted=*/true);
}

std::unique_ptr<TcpSocket> GetOneOutgoingSocket(TcpManager& mgr) {
  return GetOneSocket(mgr, /*accepted=*/false);
}

std::pair<std::unique_ptr<TcpSocket>, std::unique_ptr<TcpSocket>>
CreateTcpSocketPair(int family, bool blocking) {
  HostInfo a(TestOnly_LocalHostInfo(family, /*tcp=*/true));
  std::unique_ptr<TcpManager> mgr = TcpManager::Create(a);
  CHECK_NE(mgr, nullptr);

  // Start an acceptor thread.
  absl::Notification acceptor_started;
  util::Thread acceptor_thread([&]() {
    acceptor_started.Notify();
    mgr->Start(blocking);
  });

  // Start a connector thread.
  absl::Notification connector_started;
  util::Thread connector_thread([&]() {
    const Endpoint self = {};
    const Endpoint peer = PickPeer(a);
    connector_started.Notify();
    mgr->Connect(self, peer, blocking);
  });

  acceptor_started.WaitForNotification();
  connector_started.WaitForNotification();

  // Main thread: wait for sockets to be connected.
  std::unique_ptr<TcpSocket> sa = nullptr;
  std::unique_ptr<TcpSocket> sb = nullptr;
  while (sa == nullptr || sb == nullptr) {
    if (sa == nullptr) sa = GetOneIncomingSocket(*mgr);
    if (sb == nullptr) sb = GetOneOutgoingSocket(*mgr);
    absl::SleepFor(absl::Milliseconds(10));
  }

  mgr->Stop();
  acceptor_thread.join();
  connector_thread.join();

  CHECK(sa->IsConnected());
  CHECK(sb->IsConnected());
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

  CHECK(sa->IsConnected());
  CHECK(sb->IsConnected());
  return {std::move(sa), std::move(sb)};
}

}  // namespace peregrine::internal::testing
