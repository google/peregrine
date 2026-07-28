#include "src/internal/channel/channel_test_util.h"

#include <sys/socket.h>

#include <memory>
#include <thread>  // NOLINT
#include <utility>

#include "absl/log/check.h"
#include "absl/synchronization/notification.h"
#include "src/internal/base/endpoint.h"
#include "src/internal/channel/channel_pipe.h"
#include "src/internal/channel/channel_stream.h"
#include "src/internal/channel/channel_util.h"
#include "src/internal/socket/acceptor.h"
#include "src/internal/socket/connector.h"
#include "src/internal/socket/socket_tcp.h"
#include "src/internal/socket/socket_udp.h"
#include "src/internal/util/test_util.h"

namespace peregrine::internal::testing {

ConnectedChannelPair ConnectedChannelPair::CreateTcp(const int family) {
  const Endpoint a(TestOnly_LocalEndpoint(family, /*tcp=*/true));
  std::unique_ptr<TcpAcceptor> acceptor = TcpAcceptor::Create(a);
  CHECK_NE(acceptor, nullptr);

  std::unique_ptr<TcpSocket> sa = nullptr;
  absl::Notification acceptor_started;
  absl::Notification socket_accepted;
  auto accept = [&](std::unique_ptr<TcpSocket> socket) {
    sa = std::move(socket);
    socket_accepted.Notify();
  };
  std::jthread acceptor_thread([&]() {
    DCHECK(acceptor->Socket().IsBlocking());
    acceptor_started.Notify();
    acceptor->Start(accept);
  });

  acceptor_started.WaitForNotification();
  std::unique_ptr<TcpSocket> sb = TcpConnector::Create(/*peer=*/a);

  socket_accepted.WaitForNotification();
  acceptor->Stop();
  acceptor_thread.join();

  CHECK_NE(sa, nullptr);
  CHECK_NE(sb, nullptr);
  return {CreateTcpChannel(std::move(sa)), CreateTcpChannel(std::move(sb))};
}

ConnectedChannelPair ConnectedChannelPair::CreateUdp(const int family) {
  const Endpoint a(TestOnly_LocalEndpoint(family, /*tcp=*/false));
  const Endpoint b(TestOnly_LocalEndpoint(family, /*tcp=*/false));

  std::unique_ptr<UdpSocket> sa = TestOnly_CreateUdpSocket(family);
  std::unique_ptr<UdpSocket> sb = TestOnly_CreateUdpSocket(family);

  CHECK(sa->Bind(a));
  CHECK(sb->Bind(b));

  CHECK(sa->Connect(b));
  CHECK(sb->Connect(a));

  return {CreateUdpChannel(std::move(sa)), CreateUdpChannel(std::move(sb))};
}

ConnectedChannelPair ConnectedChannelPair::CreateMemStream() {
  auto [pipe_a, pipe_b] = BidiPipe::Create();
  auto a = std::make_unique<MemStreamChannel>(pipe_a);
  auto b = std::make_unique<MemStreamChannel>(pipe_b);
  return {std::move(a), std::move(b)};
}

}  // namespace peregrine::internal::testing
