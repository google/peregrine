#include "src/internal/channel/channel_test_util.h"

#include <sys/socket.h>

#include <memory>
#include <thread>  // NOLINT
#include <utility>

#include "absl/log/check.h"
#include "absl/time/clock.h"
#include "absl/time/time.h"
#include "src/internal/base/endpoint.h"
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
  auto accept = [&sa](std::unique_ptr<TcpSocket> socket) {
    sa = std::move(socket);
  };
  std::jthread _([&]() {
    DCHECK(acceptor->Socket().IsBlocking());
    acceptor->Start(accept);
  });

  std::unique_ptr<TcpSocket> sb = TcpConnector::Create(/*peer=*/a);

  absl::SleepFor(absl::Seconds(1));
  acceptor->Stop();

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

}  // namespace peregrine::internal::testing
