#include "peregrine/src/internal/socket/grpc_manager.h"

#include <sys/socket.h>

#include <atomic>
#include <cstddef>
#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "absl/log/check.h"
#include "absl/status/status.h"
#include "absl/synchronization/notification.h"
#include "absl/time/clock.h"
#include "absl/time/time.h"
#include "absl/types/span.h"
#include "grpc/grpc_security_constants.h"
#include "grpcpp/security/credentials.h"
#include "grpcpp/security/server_credentials.h"
#include "grpcpp/support/byte_buffer.h"
#include "grpcpp/support/slice.h"
#include "peregrine/src/api/transport_types.h"
#include "peregrine/src/internal/base/endpoint.h"
#include "peregrine/src/internal/base/types.h"
#include "peregrine/src/internal/util/test_param.h"
#include "peregrine/src/util/thread.h"
#include "peregrine/src/util/util.h"

namespace peregrine::internal::testing {
namespace {

using ::testing::Each;
using ::testing::IsEmpty;
using ::testing::NotNull;
using ::testing::Optional;
using ::testing::TestParamInfo;
using ::testing::TestWithParam;
using ::testing::UnorderedElementsAre;
using ::testing::Values;

// Long enough for a stream to be accepted, or for a closed stream to notice
// that it is closed, on a slow test machine.
constexpr absl::Duration kLongTimeout = absl::Seconds(30);
// Short enough to wait for something that is not expected to happen.
constexpr absl::Duration kShortTimeout = absl::Milliseconds(10);

// Returns the loopback endpoint of the address `family` with port 0.
Endpoint Loopback(const int family) {
  return Endpoint::Create(family == AF_INET ? "127.0.0.1:0" : "[::1]:0");
}

// Creates a manager listening on a free port of the loopback address.
// The tests use gRPC local credentials, which only allow loopback connections.
std::unique_ptr<GrpcManager> CreateManager(const int family) {
  auto manager = GrpcManager::Create(
      Loopback(family), grpc::experimental::LocalServerCredentials(LOCAL_TCP));
  CHECK_OK(manager.status());
  return std::move(*manager);
}

// Creates the client side of a new stream to the `manager`.
std::unique_ptr<GrpcStream> Connect(const GrpcManager& manager,
                                    const int family) {
  Endpoint peer = Loopback(family);
  peer.SetPort(static_cast<port_t>(manager.Port()));
  return GrpcManager::Connect(peer,
                              grpc::experimental::LocalCredentials(LOCAL_TCP));
}

// Returns `size` random bytes.
std::vector<Byte> RandomBytes(const size_t size) {
  std::vector<Byte> data(size);
  if (size > 0) util::RandomNonZero(absl::MakeSpan(data));
  return data;
}

// The outcome of one send or receive of a stream. It is shared with the
// callback of the operation, which can be called after the test has ended.
struct Outcome {
  absl::Notification done;  // Notified when the callback is called.
  bool ok = false;
  std::vector<Byte> data;  // The received message.
};

// Starts sending the `data` as one message.
std::shared_ptr<Outcome> StartSend(GrpcStream& stream,
                                   const std::vector<Byte>& data) {
  auto outcome = std::make_shared<Outcome>();
  const grpc::Slice slice(data.data(), data.size());
  grpc::ByteBuffer msg(&slice, 1);
  stream.Send(&msg, [outcome](bool ok) {
    outcome->ok = ok;
    outcome->done.Notify();
  });
  return outcome;
}

// Starts receiving one message.
std::shared_ptr<Outcome> StartRecv(GrpcStream& stream) {
  auto outcome = std::make_shared<Outcome>();
  stream.Recv([outcome](bool ok, grpc::ByteBuffer* msg) {
    outcome->ok = ok;
    std::vector<grpc::Slice> slices;
    if (msg->Length() > 0) CHECK(msg->Dump(&slices).ok());
    for (const grpc::Slice& slice : slices) {
      outcome->data.insert(outcome->data.end(), slice.begin(), slice.end());
    }
    outcome->done.Notify();
  });
  return outcome;
}

// Sends the `data` as one message and waits for the send to end. Returns true
// iff the message is sent.
bool Send(GrpcStream& stream, const std::vector<Byte>& data) {
  const std::shared_ptr<Outcome> send = StartSend(stream, data);
  send->done.WaitForNotification();
  return send->ok;
}

// Receives one message and waits for the receive to end. Returns the data of
// the message, or nullopt if there is no more message to receive.
std::optional<std::vector<Byte>> Recv(GrpcStream& stream) {
  const std::shared_ptr<Outcome> recv = StartRecv(stream);
  recv->done.WaitForNotification();
  if (!recv->ok) return std::nullopt;
  return recv->data;
}

// Counts the operations of a chain that succeeded.
struct Chain {
  absl::Notification done;  // Notified when the chain ends.
  std::atomic<int> num_ok = 0;
};

// Sends `n` one-byte messages, each from the callback of the previous send.
// The chain ends early if a send fails.
void SendChain(GrpcStream* stream, const int n, std::shared_ptr<Chain> chain) {
  if (n == 0) {
    chain->done.Notify();
    return;
  }
  const grpc::Slice slice("x", 1);
  grpc::ByteBuffer msg(&slice, 1);
  stream->Send(&msg, [stream, n, chain](bool ok) {
    if (!ok) {
      chain->done.Notify();
      return;
    }
    ++chain->num_ok;
    SendChain(stream, n - 1, chain);
  });
}

// Receives `n` one-byte messages, each from the callback of the previous
// receive. The chain ends early if a receive fails.
void RecvChain(GrpcStream* stream, const int n, std::shared_ptr<Chain> chain) {
  if (n == 0) {
    chain->done.Notify();
    return;
  }
  stream->Recv([stream, n, chain](bool ok, grpc::ByteBuffer* msg) {
    if (!ok || msg->Length() != 1) {
      chain->done.Notify();
      return;
    }
    ++chain->num_ok;
    RecvChain(stream, n - 1, chain);
  });
}

// Keeps sending one-byte messages until a send fails. Returns true iff a send
// fails within `kLongTimeout`. A stream closed by its peer can accept a few
// more messages before it learns that the peer is gone.
bool SendEventuallyFails(GrpcStream& stream) {
  const absl::Time deadline = absl::Now() + kLongTimeout;
  while (absl::Now() < deadline) {
    if (!Send(stream, {1})) return true;
  }
  return false;
}

// Tests `GrpcManager` on the loopback address of the address family given by
// the test parameter.
class GrpcManagerTest : public TestWithParam<int> {
 protected:
  int family() const { return GetParam(); }
};

INSTANTIATE_TEST_SUITE_P(, GrpcManagerTest, Values(AF_INET, AF_INET6),
                         [](const TestParamInfo<int>& info) {
                           return FamilyToString(info.param);
                         });

TEST_P(GrpcManagerTest, ListensOnFreePort) {
  const std::unique_ptr<GrpcManager> a = CreateManager(family());
  const std::unique_ptr<GrpcManager> b = CreateManager(family());

  EXPECT_GT(a->Port(), 0);
  EXPECT_GT(b->Port(), 0);
  EXPECT_NE(a->Port(), b->Port());
}

TEST_P(GrpcManagerTest, AcceptTimesOutWhenNothingConnected) {
  const std::unique_ptr<GrpcManager> manager = CreateManager(family());
  constexpr absl::Duration kTimeout = absl::Milliseconds(50);

  const absl::Time start = absl::Now();
  EXPECT_EQ(manager->Accept(kTimeout), nullptr);
  EXPECT_GE(absl::Now() - start, kTimeout);
}

TEST_P(GrpcManagerTest, AcceptReturnsStreamConnectedToClient) {
  const std::unique_ptr<GrpcManager> manager = CreateManager(family());
  const std::unique_ptr<GrpcStream> client = Connect(*manager, family());

  const std::unique_ptr<GrpcStream> server = manager->Accept(kLongTimeout);

  ASSERT_NE(server, nullptr);
  const std::vector<Byte> request = RandomBytes(1000);
  const std::vector<Byte> response = RandomBytes(2000);
  ASSERT_TRUE(Send(*client, request));
  EXPECT_THAT(Recv(*server), Optional(request));
  ASSERT_TRUE(Send(*server, response));
  EXPECT_THAT(Recv(*client), Optional(response));
}

TEST_P(GrpcManagerTest, AcceptWaitsForClientToConnect) {
  const std::unique_ptr<GrpcManager> manager = CreateManager(family());
  std::unique_ptr<GrpcStream> server;

  util::Thread acceptor([&]() { server = manager->Accept(kLongTimeout); });
  // Gives the acceptor some time to block. The test passes in either order.
  absl::SleepFor(absl::Milliseconds(100));
  const std::unique_ptr<GrpcStream> client = Connect(*manager, family());
  acceptor.join();

  EXPECT_NE(server, nullptr);
}

TEST_P(GrpcManagerTest, AcceptsOneStreamPerConnect) {
  constexpr int kNumStreams = 3;
  const std::unique_ptr<GrpcManager> manager = CreateManager(family());
  // Each client side sends its index to whichever server side it is paired to.
  std::vector<std::unique_ptr<GrpcStream>> clients;
  for (int i = 0; i < kNumStreams; ++i) {
    clients.push_back(Connect(*manager, family()));
    ASSERT_TRUE(Send(*clients.back(), {static_cast<Byte>(i)}))
        << "client " << i;
  }

  std::vector<std::unique_ptr<GrpcStream>> servers;
  std::vector<std::vector<Byte>> indexes;
  for (int i = 0; i < kNumStreams; ++i) {
    servers.push_back(manager->Accept(kLongTimeout));
    ASSERT_NE(servers.back(), nullptr) << "server " << i;
    const std::optional<std::vector<Byte>> index = Recv(*servers.back());
    ASSERT_TRUE(index.has_value()) << "server " << i;
    indexes.push_back(*index);
  }

  EXPECT_THAT(indexes,
              UnorderedElementsAre(std::vector<Byte>{0}, std::vector<Byte>{1},
                                   std::vector<Byte>{2}));
  EXPECT_EQ(manager->Accept(kShortTimeout), nullptr);
}

TEST_P(GrpcManagerTest, ConcurrentConnectAndAccept) {
  constexpr int kNumThreads = 4;
  constexpr int kNumStreams = 8;
  const std::unique_ptr<GrpcManager> manager = CreateManager(family());
  // The streams are kept until all the threads are done, and each thread only
  // touches its own vector.
  std::vector<std::vector<std::unique_ptr<GrpcStream>>> clients(kNumThreads);
  std::vector<std::vector<std::unique_ptr<GrpcStream>>> servers(kNumThreads);

  std::vector<util::Thread> threads;
  for (int t = 0; t < kNumThreads; ++t) {
    threads.emplace_back([&, t]() {
      for (int i = 0; i < kNumStreams; ++i) {
        clients[t].push_back(Connect(*manager, family()));
      }
    });
    threads.emplace_back([&, t]() {
      for (int i = 0; i < kNumStreams; ++i) {
        servers[t].push_back(manager->Accept(kLongTimeout));
      }
    });
  }
  for (util::Thread& thread : threads) {
    thread.join();
  }

  for (int t = 0; t < kNumThreads; ++t) {
    EXPECT_THAT(servers[t], Each(NotNull())) << "acceptor thread " << t;
  }
  EXPECT_EQ(manager->Accept(kShortTimeout), nullptr);
}

TEST_P(GrpcManagerTest, StopClosesAcceptedStreams) {
  const std::unique_ptr<GrpcManager> manager = CreateManager(family());
  const std::unique_ptr<GrpcStream> client = Connect(*manager, family());
  const std::unique_ptr<GrpcStream> server = manager->Accept(kLongTimeout);
  ASSERT_NE(server, nullptr);

  manager->Stop();

  EXPECT_EQ(Recv(*server), std::nullopt);
  EXPECT_TRUE(SendEventuallyFails(*server));
  EXPECT_EQ(Recv(*client), std::nullopt);
  EXPECT_TRUE(SendEventuallyFails(*client));
}

TEST_P(GrpcManagerTest, StopTwiceIsSameAsOnce) {
  const std::unique_ptr<GrpcManager> manager = CreateManager(family());
  const int port = manager->Port();

  manager->Stop();
  manager->Stop();

  EXPECT_EQ(manager->Port(), port);
  EXPECT_EQ(manager->Accept(kShortTimeout), nullptr);
}

TEST_P(GrpcManagerTest, ConnectAfterStopReturnsClosedStream) {
  const std::unique_ptr<GrpcManager> manager = CreateManager(family());
  manager->Stop();

  const std::unique_ptr<GrpcStream> client = Connect(*manager, family());

  ASSERT_NE(client, nullptr);
  EXPECT_EQ(Recv(*client), std::nullopt);
  EXPECT_TRUE(SendEventuallyFails(*client));
  EXPECT_EQ(manager->Accept(kShortTimeout), nullptr);
}

TEST_P(GrpcManagerTest, DestroyedManagerClosesAcceptedStreams) {
  std::unique_ptr<GrpcManager> manager = CreateManager(family());
  const std::unique_ptr<GrpcStream> client = Connect(*manager, family());
  const std::unique_ptr<GrpcStream> server = manager->Accept(kLongTimeout);
  ASSERT_NE(server, nullptr);

  manager = nullptr;

  // The streams outlive the manager and are destroyed after it.
  EXPECT_EQ(Recv(*server), std::nullopt);
  EXPECT_TRUE(SendEventuallyFails(*server));
  EXPECT_EQ(Recv(*client), std::nullopt);
  EXPECT_TRUE(SendEventuallyFails(*client));
}

TEST_P(GrpcManagerTest, DestroyedManagerClosesStreamNotAcceptedYet) {
  std::unique_ptr<GrpcManager> manager = CreateManager(family());
  const std::unique_ptr<GrpcStream> client = Connect(*manager, family());
  // Gives the manager some time to queue the server side of the stream. The
  // test passes in either order.
  absl::SleepFor(absl::Milliseconds(100));

  manager = nullptr;

  EXPECT_EQ(Recv(*client), std::nullopt);
  EXPECT_TRUE(SendEventuallyFails(*client));
}

TEST(GrpcManagerCreateTest, NullCredentialsReturnsInvalidArgument) {
  const auto manager =
      GrpcManager::Create(Loopback(AF_INET), /*creds=*/nullptr);

  EXPECT_EQ(manager.status().code(), absl::StatusCode::kInvalidArgument);
}

TEST(GrpcManagerCreateTest, AddressNotOnThisHostReturnsInternal) {
  // 192.0.2.0/24 is reserved for documentation (RFC 5737), so no network
  // interface of the host has this address.
  const Endpoint self = Endpoint::Create("192.0.2.1:0");

  const auto manager = GrpcManager::Create(
      self, grpc::experimental::LocalServerCredentials(LOCAL_TCP));

  EXPECT_EQ(manager.status().code(), absl::StatusCode::kInternal);
}

// Tests one `GrpcStream` in one direction: the test parameter is true iff the
// sender is the client side of the stream. Both directions are tested because
// the two sides of a stream are implemented differently.
class GrpcStreamTest : public TestWithParam<bool> {
 protected:
  GrpcStreamTest()
      : manager_(CreateManager(AF_INET)),
        client_(Connect(*manager_, AF_INET)),
        server_(manager_->Accept(kLongTimeout)),
        sndr_(GetParam() ? client_ : server_),
        rcvr_(GetParam() ? server_ : client_) {
    CHECK_NE(server_, nullptr);
  }

 protected:
  // The manager is destroyed after the streams.
  const std::unique_ptr<GrpcManager> manager_;
  std::unique_ptr<GrpcStream> client_;
  std::unique_ptr<GrpcStream> server_;
  std::unique_ptr<GrpcStream>& sndr_;
  std::unique_ptr<GrpcStream>& rcvr_;
};

INSTANTIATE_TEST_SUITE_P(, GrpcStreamTest, Values(true, false),
                         [](const TestParamInfo<bool>& info) {
                           return info.param ? "ClientToServer"
                                             : "ServerToClient";
                         });

TEST_P(GrpcStreamTest, SendRecv) {
  const std::vector<Byte> first = RandomBytes(1000);
  const std::vector<Byte> reply = RandomBytes(2000);
  const std::vector<Byte> second = RandomBytes(3000);

  ASSERT_TRUE(Send(*sndr_, first));
  EXPECT_THAT(Recv(*rcvr_), Optional(first));
  ASSERT_TRUE(Send(*rcvr_, reply));
  EXPECT_THAT(Recv(*sndr_), Optional(reply));
  ASSERT_TRUE(Send(*sndr_, second));
  EXPECT_THAT(Recv(*rcvr_), Optional(second));
}

TEST_P(GrpcStreamTest, RecvStartedBeforeMessageArrivesEndsWithIt) {
  const std::vector<Byte> data = RandomBytes(1000);

  const std::shared_ptr<Outcome> recv = StartRecv(*rcvr_);
  const bool ended_without_message =
      recv->done.WaitForNotificationWithTimeout(kShortTimeout);
  ASSERT_TRUE(Send(*sndr_, data));
  recv->done.WaitForNotification();

  EXPECT_FALSE(ended_without_message);
  EXPECT_TRUE(recv->ok);
  EXPECT_EQ(recv->data, data);
}

TEST_P(GrpcStreamTest, CallbackCanStartNextOperation) {
  constexpr int kNumMessages = 100;
  const auto sends = std::make_shared<Chain>();
  const auto recvs = std::make_shared<Chain>();

  RecvChain(rcvr_.get(), kNumMessages, recvs);
  SendChain(sndr_.get(), kNumMessages, sends);
  sends->done.WaitForNotification();
  recvs->done.WaitForNotification();

  EXPECT_EQ(sends->num_ok, kNumMessages);
  EXPECT_EQ(recvs->num_ok, kNumMessages);
}

TEST_P(GrpcStreamTest, CallbackCanShutDownStream) {
  const auto shut_down = std::make_shared<absl::Notification>();

  rcvr_->Recv([stream = rcvr_.get(), shut_down](bool, grpc::ByteBuffer*) {
    stream->Shutdown();
    shut_down->Notify();
  });
  ASSERT_TRUE(Send(*sndr_, {1}));
  shut_down->WaitForNotification();

  EXPECT_FALSE(Send(*rcvr_, {1}));
  EXPECT_EQ(Recv(*sndr_), std::nullopt);
}

TEST_P(GrpcStreamTest, KeepsMessageBoundaries) {
  const std::vector<Byte> small = RandomBytes(1);
  const std::vector<Byte> medium = RandomBytes(1000);
  const std::vector<Byte> large = RandomBytes(100'000);

  ASSERT_TRUE(Send(*sndr_, small));
  ASSERT_TRUE(Send(*sndr_, medium));
  ASSERT_TRUE(Send(*sndr_, large));

  EXPECT_THAT(Recv(*rcvr_), Optional(small));
  EXPECT_THAT(Recv(*rcvr_), Optional(medium));
  EXPECT_THAT(Recv(*rcvr_), Optional(large));
}

TEST_P(GrpcStreamTest, EmptyMessageIsStillOneMessage) {
  const std::vector<Byte> next = RandomBytes(10);

  ASSERT_TRUE(Send(*sndr_, {}));
  ASSERT_TRUE(Send(*sndr_, next));

  EXPECT_THAT(Recv(*rcvr_), Optional(IsEmpty()));
  EXPECT_THAT(Recv(*rcvr_), Optional(next));
}

TEST_P(GrpcStreamTest, MessageBiggerThanGrpcDefaultLimit) {
  // gRPC rejects a received message of more than 4 MiB by default.
  const std::vector<Byte> data = RandomBytes(8 << 20);

  // The receive starts first, because the send of a message of this size does
  // not end until the peer receives.
  const std::shared_ptr<Outcome> recv = StartRecv(*rcvr_);
  ASSERT_TRUE(Send(*sndr_, data));
  recv->done.WaitForNotification();

  ASSERT_TRUE(recv->ok);
  EXPECT_TRUE(recv->data == data);
}

TEST_P(GrpcStreamTest, ConcurrentSendRecv) {
  constexpr int kNumMessages = 100;
  constexpr size_t kSize = 10'000;
  std::atomic<int> sent = 0;
  std::atomic<int> rcvd = 0;
  auto send = [&sent](GrpcStream* stream) {
    const std::vector<Byte> data = RandomBytes(kSize);
    for (int i = 0; i < kNumMessages; ++i) {
      if (Send(*stream, data)) ++sent;
    }
  };
  auto recv = [&rcvd](GrpcStream* stream) {
    for (int i = 0; i < kNumMessages; ++i) {
      const std::optional<std::vector<Byte>> data = Recv(*stream);
      if (data.has_value() && data->size() == kSize) ++rcvd;
    }
  };

  // Each side of the stream sends and receives at the same time.
  std::vector<util::Thread> threads;
  for (GrpcStream* stream : {sndr_.get(), rcvr_.get()}) {
    threads.emplace_back(send, stream);
    threads.emplace_back(recv, stream);
  }
  for (util::Thread& thread : threads) {
    thread.join();
  }

  EXPECT_EQ(sent, 2 * kNumMessages);
  EXPECT_EQ(rcvd, 2 * kNumMessages);
}

TEST_P(GrpcStreamTest, ShutdownFailsSendAndRecv) {
  sndr_->Shutdown();

  EXPECT_FALSE(Send(*sndr_, {1}));
  EXPECT_EQ(Recv(*sndr_), std::nullopt);
}

TEST_P(GrpcStreamTest, ShutdownTwiceIsSameAsOnce) {
  sndr_->Shutdown();
  sndr_->Shutdown();

  EXPECT_FALSE(Send(*sndr_, {1}));
  EXPECT_EQ(Recv(*sndr_), std::nullopt);
}

TEST_P(GrpcStreamTest, PeerSeesEndOfMessagesAfterShutdown) {
  sndr_->Shutdown();

  EXPECT_EQ(Recv(*rcvr_), std::nullopt);
}

TEST_P(GrpcStreamTest, PeerReceivesMessageSentBeforeShutdown) {
  const std::vector<Byte> data = RandomBytes(1000);
  ASSERT_TRUE(Send(*sndr_, data));

  sndr_->Shutdown();

  EXPECT_THAT(Recv(*rcvr_), Optional(data));
  EXPECT_EQ(Recv(*rcvr_), std::nullopt);
}

TEST_P(GrpcStreamTest, ShutdownFailsRecvInProgress) {
  // The receive stays in progress because the peer sends nothing.
  const std::shared_ptr<Outcome> recv = StartRecv(*sndr_);

  sndr_->Shutdown();
  recv->done.WaitForNotification();

  EXPECT_FALSE(recv->ok);
}

TEST_P(GrpcStreamTest, ShutdownFailsSendInProgress) {
  const std::vector<Byte> data = RandomBytes(1 << 20);
  // A send eventually stays in progress because the peer receives nothing, so
  // the stream cannot finish sending a message.
  std::shared_ptr<Outcome> send = StartSend(*sndr_, data);
  while (send->done.WaitForNotificationWithTimeout(absl::Milliseconds(500))) {
    ASSERT_TRUE(send->ok);
    send = StartSend(*sndr_, data);
  }

  sndr_->Shutdown();
  send->done.WaitForNotification();

  EXPECT_FALSE(send->ok);
  EXPECT_FALSE(Send(*sndr_, {1}));
}

TEST_P(GrpcStreamTest, DestroyedStreamFailsRecvInProgress) {
  const std::shared_ptr<Outcome> recv = StartRecv(*sndr_);

  sndr_ = nullptr;

  // The callback is called before the destructor returns.
  ASSERT_TRUE(recv->done.HasBeenNotified());
  EXPECT_FALSE(recv->ok);
}

TEST_P(GrpcStreamTest, DestroyedPeerClosesStream) {
  sndr_ = nullptr;

  EXPECT_EQ(Recv(*rcvr_), std::nullopt);
  EXPECT_TRUE(SendEventuallyFails(*rcvr_));
}

}  // namespace
}  // namespace peregrine::internal::testing
