#include "peregrine/src/internal/channel/channel_test_util.h"

#include <sys/socket.h>

#include <memory>
#include <string>
#include <utility>

#include "absl/log/check.h"
#include "absl/time/time.h"
#include "grpc/grpc_security_constants.h"
#include "grpcpp/security/credentials.h"
#include "grpcpp/security/server_credentials.h"
#include "peregrine/src/internal/base/endpoint.h"
#include "peregrine/src/internal/base/types.h"
#include "peregrine/src/internal/channel/channel.h"
#include "peregrine/src/internal/channel/channel_grpc.h"
#include "peregrine/src/internal/channel/channel_msg.h"
#include "peregrine/src/internal/channel/channel_stream.h"
#include "peregrine/src/internal/channel/channel_type.h"
#include "peregrine/src/internal/channel/channel_util.h"
#include "peregrine/src/internal/channel/pipe.h"
#include "peregrine/src/internal/lib/iovec_cursor.h"
#include "peregrine/src/internal/socket/grpc_manager.h"
#include "peregrine/src/internal/socket/socket_test_util.h"
#include "peregrine/src/internal/util/test_param.h"

namespace peregrine::internal::testing {

ConnectedChannelPair CreateTcpChannelPair(const int family,
                                          const bool blocking) {
  auto [sa, sb] = CreateTcpSocketPair(family, blocking);
  DCHECK_NE(sa, nullptr);
  DCHECK_NE(sb, nullptr);
  return {CreateTcpChannel(std::move(sa)), CreateTcpChannel(std::move(sb))};
}

ConnectedChannelPair CreateUdpChannelPair(const int family,
                                          const bool blocking) {
  auto [sa, sb] = CreateUdpSocketPair(family, blocking);
  DCHECK_NE(sa, nullptr);
  DCHECK_NE(sb, nullptr);
  return {CreateUdpChannel(std::move(sa)), CreateUdpChannel(std::move(sb))};
}

namespace {

// The server side of a gRPC channel together with the manager that accepted
// it, because the manager has to outlive the channel.
class ListeningGrpcChannel final : public Channel {
 public:
  // Constructor.
  ListeningGrpcChannel(std::unique_ptr<GrpcManager> manager,
                       std::unique_ptr<Channel> channel)
      : manager_(std::move(manager)), channel_(std::move(channel)) {
    DCHECK_NE(manager_, nullptr);
    DCHECK_NE(channel_, nullptr);
  }

  ChannelType Type() const override { return channel_->Type(); }
  bool IsBlocking() const override { return channel_->IsBlocking(); }
  Endpoint GetPeer() const override { return channel_->GetPeer(); }
  void SetPeer(const Endpoint& peer) override { channel_->SetPeer(peer); }
  ssize_t Write(IoVecCursor& iovecs) override {
    return channel_->Write(iovecs);
  }
  ssize_t Read(IoVecCursor& iovecs) override { return channel_->Read(iovecs); }
  void Shutdown() override { channel_->Shutdown(); }
  std::string ToString() const override { return channel_->ToString(); }

 private:
  // The channel is destroyed before the manager.
  const std::unique_ptr<GrpcManager> manager_;
  const std::unique_ptr<Channel> channel_;
};

}  // namespace

ConnectedChannelPair CreateGrpcChannelPair(const int family) {
  DCHECK(family == AF_INET || family == AF_INET6);
  constexpr absl::Duration kAcceptTimeout = absl::Seconds(30);

  // Listens on a free port of the loopback address, with gRPC local
  // credentials, which only allow loopback connections.
  Endpoint endpoint =
      Endpoint::Create(family == AF_INET ? "127.0.0.1:0" : "[::1]:0");
  auto manager = GrpcManager::Create(
      endpoint, grpc::experimental::LocalServerCredentials(LOCAL_TCP));
  CHECK_OK(manager.status());  // Crash OK
  endpoint.SetPort(static_cast<port_t>((*manager)->Port()));

  auto client = GrpcManager::Connect(
      endpoint, grpc::experimental::LocalCredentials(LOCAL_TCP));
  auto server = (*manager)->Accept(kAcceptTimeout);
  CHECK_NE(server, nullptr);  // Crash OK
  auto sndr = std::make_unique<GrpcChannel>(std::move(client));
  auto rcvr = std::make_unique<GrpcChannel>(std::move(server));
  return {std::move(sndr), std::make_unique<ListeningGrpcChannel>(
                               std::move(*manager), std::move(rcvr))};
}

ConnectedChannelPair CreateMemStreamChannelPair(const bool blocking,
                                                const int error_rate) {
  auto [pipe_a, pipe_b] = BidiPipe::Create();
  auto a = std::make_unique<MemStreamChannel>(pipe_a, blocking, error_rate);
  auto b = std::make_unique<MemStreamChannel>(pipe_b, blocking, error_rate);
  return {std::move(a), std::move(b)};
}

ConnectedChannelPair CreateMemMsgChannelPair(const bool blocking,
                                             const int error_rate) {
  auto [pipe_a, pipe_b] = BidiPipe::Create();
  auto a = std::make_unique<MemMsgChannel>(pipe_a, blocking, error_rate);
  auto b = std::make_unique<MemMsgChannel>(pipe_b, blocking, error_rate);
  return {std::move(a), std::move(b)};
}

ConnectedChannelPair CreateTestChannelPair(const ChannelTestParam& p) {
  switch (p.type) {
    case TestChannelType::kTcp:
      return CreateTcpChannelPair(p.family, p.blocking);
    case TestChannelType::kUdp:
      return CreateUdpChannelPair(p.family, p.blocking);
    case TestChannelType::kGrpc:
      DCHECK(p.blocking);
      return CreateGrpcChannelPair(p.family);
    case TestChannelType::kMemStream:
      return CreateMemStreamChannelPair(p.blocking, p.error_rate);
    case TestChannelType::kMemMsg:
      return CreateMemMsgChannelPair(p.blocking, p.error_rate);
  }
}

}  // namespace peregrine::internal::testing
