#include "src/internal/channel/channel.h"

#include <cstddef>
#include <memory>
#include <thread>  // NOLINT
#include <utility>
#include <vector>

#include "gtest/gtest.h"
#include "absl/log/check.h"
#include "absl/log/log.h"
#include "absl/time/clock.h"
#include "absl/time/time.h"
#include "src/api/types.h"
#include "src/internal/base/endpoint.h"
#include "src/internal/base/types.h"
#include "src/internal/channel/channel_test_util.h"
#include "src/internal/channel/channel_util.h"
#include "src/internal/chunk/chunk.h"
#include "src/internal/chunk/chunk_test_util.h"
#include "src/internal/socket/acceptor.h"
#include "src/internal/socket/connector.h"
#include "src/internal/socket/socket_tcp.h"
#include "src/internal/socket/socket_udp.h"
#include "src/internal/util/test_util.h"

namespace peregrine::internal::testing {
namespace {

template <typename T>
T* Ptr(ChunkMetadata& metadata) {
  return reinterpret_cast<T*>(&metadata);
}

struct ConnectedChannelPair {
  std::unique_ptr<Channel> sndr;
  std::unique_ptr<Channel> rcvr;
};

ConnectedChannelPair CreateTcpChannelPair() {
  constexpr int kFamily = AF_INET;
  const Endpoint local(TestOnly_LocalEndpoint(kFamily, /*tcp=*/true));
  std::unique_ptr<TcpAcceptor> acceptor = TcpAcceptor::Create(local);
  CHECK_NE(acceptor, nullptr);

  std::unique_ptr<TcpSocket> sa = nullptr;
  auto accept = [&sa](std::unique_ptr<TcpSocket> socket) {
    sa = std::move(socket);
  };
  std::jthread _([&]() {
    DCHECK(acceptor->Socket().IsBlocking());
    acceptor->Start(accept);
  });

  std::unique_ptr<TcpSocket> sb = TcpConnector::Create(/*peer=*/local);

  absl::SleepFor(absl::Seconds(1));
  acceptor->Stop();

  CHECK_NE(sa, nullptr);
  CHECK_NE(sb, nullptr);
  return {CreateTcpChannel(std::move(sa)), CreateTcpChannel(std::move(sb))};
}

ConnectedChannelPair CreateUdpChannelPair() {
  constexpr int kFamily = AF_INET6;
  const Endpoint a(TestOnly_LocalEndpoint(kFamily, /*tcp=*/false));
  const Endpoint b(TestOnly_LocalEndpoint(kFamily, /*tcp=*/false));
  std::unique_ptr<UdpSocket> sa = TestOnly_CreateUdpSocket(kFamily);
  std::unique_ptr<UdpSocket> sb = TestOnly_CreateUdpSocket(kFamily);
  CHECK(sa->Bind(a));
  CHECK(sb->Bind(b));
  CHECK(sa->Connect(b));
  CHECK(sb->Connect(a));
  return {CreateUdpChannel(std::move(sa)), CreateUdpChannel(std::move(sb))};
}

TEST(ReliableStreamChannelTest, ReadWrite) {
  // Create channel pairs.
  ConnectedChannelPair tcp = CreateTcpChannelPair();
  std::unique_ptr<Channel> mem = TestOnly_CreateMemStreamChannel();

  // Get the channel pointers.
  std::pair<Channel*, Channel*> tcp_chs = {tcp.sndr.get(), tcp.rcvr.get()};
  std::pair<Channel*, Channel*> mem_chs = {mem.get(), mem.get()};

  // Read and write on the channel pair.
  for (auto [sndr, rcvr] : {tcp_chs, mem_chs}) {
    ASSERT_TRUE(IsReliableStream(sndr->Type()));
    ASSERT_TRUE(IsReliableStream(rcvr->Type()));

    // Send to one channel.
    ChunkMetadata in = testing::GenChunkMetadata();
    constexpr size_t kSize = sizeof(ChunkMetadata);
    constexpr int kN = 2;
    for (int i = 0; i < kN; ++i) {
      EXPECT_TRUE(sndr->Write({{Ptr<void>(in), kSize}}));
    }

    // Receive from the other channel.
    ChunkMetadata out[kN];
    for (int i = 0; i < kN; ++i) {
      EXPECT_EQ(rcvr->Read(Ptr<Byte>(out[i]), kSize), kSize);
    }

    // Check that the data read is the same as written.
    for (int i = 0; i < kN; ++i) {
      EXPECT_EQ(in, out[i]);
    }
    LOG(INFO) << *sndr;
    LOG(INFO) << *rcvr;
  }
}

TEST(UnreliableMessageChannelTest, ReadWrite) {
  // Create channel pairs.
  ConnectedChannelPair udp = CreateUdpChannelPair();
  std::unique_ptr<Channel> mem = TestOnly_CreateMemMsgChannel(/*error=*/0);

  // Get the channel pointers.
  std::pair<Channel*, Channel*> udp_chs = {udp.sndr.get(), udp.rcvr.get()};
  std::pair<Channel*, Channel*> mem_chs = {mem.get(), mem.get()};

  // Read and write on the channel pair.
  for (auto [sndr, rcvr] : {udp_chs, mem_chs}) {
    ASSERT_TRUE(IsUnreliableMessage(sndr->Type()));
    ASSERT_TRUE(IsUnreliableMessage(rcvr->Type()));

    // Write to one channel.
    ChunkMetadata in = testing::GenChunkMetadata();
    constexpr size_t kSize = sizeof(ChunkMetadata);
    constexpr int kN = 2;
    std::vector<IoVec> iovecs;
    for (int i = 0; i < kN; ++i) {
      iovecs.push_back({Ptr<void>(in), kSize});
    }
    EXPECT_TRUE(sndr->Write(iovecs));

    // Read from the other channel.
    ChunkMetadata out[kN];
    EXPECT_EQ(rcvr->Read(Ptr<Byte>(out[0]), kN * kSize), kN * kSize);

    // Check that the data read is the same as written.
    for (int i = 0; i < kN; ++i) {
      EXPECT_EQ(in, out[i]);
    }
    LOG(INFO) << *sndr;
    LOG(INFO) << *rcvr;
  }
}

}  // namespace
}  // namespace peregrine::internal::testing
