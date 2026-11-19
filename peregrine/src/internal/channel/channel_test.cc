#include "peregrine/src/internal/channel/channel.h"

#include <sys/epoll.h>
#include <sys/socket.h>
#include <sys/types.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "absl/log/check.h"
#include "absl/log/log.h"
#include "absl/time/clock.h"
#include "absl/time/time.h"
#include "absl/types/span.h"
#include "grpc/grpc_security_constants.h"
#include "grpcpp/security/credentials.h"
#include "grpcpp/security/server_credentials.h"
#include "peregrine/src/api/transport_types.h"
#include "peregrine/src/internal/base/endpoint.h"
#include "peregrine/src/internal/base/types.h"
#include "peregrine/src/internal/channel/channel_grpc.h"
#include "peregrine/src/internal/channel/channel_test_util.h"
#include "peregrine/src/internal/channel/channel_type.h"
#include "peregrine/src/internal/event/poller.h"
#include "peregrine/src/internal/lib/iovec_cursor.h"
#include "peregrine/src/internal/socket/grpc_manager.h"
#include "peregrine/src/internal/util/test_param.h"
#include "peregrine/src/util/thread.h"
#include "peregrine/src/util/util.h"

namespace peregrine::internal::testing {
namespace {

using TestChannelType::kGrpc;
using TestChannelType::kMemMsg;
using TestChannelType::kMemStream;
using TestChannelType::kTcp;
using TestChannelType::kUdp;
using ::testing::Eq;
using ::testing::Ne;
using ::testing::Pointwise;
using ::testing::Values;

using Param = ChannelTestParam;

std::string ToString(const ::testing::TestParamInfo<Param>& info) {
  return info.param.ToString();
}

std::unique_ptr<Poller> CreateEventPoller(const Channel& ch, uint32_t events) {
  std::unique_ptr<Poller> poller = Poller::Create();
  CHECK_NE(poller, nullptr);
  const fd_t fd = ch.GetFd();
  if (fd.value() >= 0) {
    CHECK_EQ(poller->Register(fd, events), 0);
  }
  return poller;
}

uint32_t WaitForEvents(Poller& poller, const Channel& ch) {
  if (ch.GetFd().value() < 0) {
    return EPOLLIN | EPOLLOUT;
  }
  epoll_event event;
  for (int i = 0; i < 100; ++i) {
    const int nfds =
        poller.BlockingWait(&event, /*max_events=*/1, /*timeout_ms=*/100);
    if (nfds > 0) return event.events;
    if (nfds < 0) return 0;
  }
  return 0;
}

class ChannelTest : public ::testing::TestWithParam<Param> {
 protected:
  ChannelTest()
      : p_(GetParam()),
        size_(p_.size),
        part_(size_ / 4),
        src_(size_),
        dst_(size_, 0),
        siovs_({
            {src_.data() + 0 * part_, part_},
            {src_.data() + 1 * part_, part_},
            {src_.data() + 2 * part_, part_},
            {src_.data() + 3 * part_, part_},
        }),
        diovs_({
            {dst_.data() + 0 * part_, part_},
            {dst_.data() + 1 * part_, part_},
            {dst_.data() + 2 * part_, part_ * 2},
        }) {
    util::RandomNonZero(absl::MakeSpan(src_));
    DCHECK_NE(src_.data(), dst_.data());
    CHECK_EQ(size_, 4 * part_);
  }

 protected:
  const Param p_;
  const size_t size_;
  const size_t part_;
  std::vector<Byte> src_;
  std::vector<Byte> dst_;

  static constexpr int kSrc = 4;
  static constexpr int kDst = 3;
  std::array<IoVec, kSrc> siovs_;
  std::array<IoVec, kDst> diovs_;
};

INSTANTIATE_TEST_SUITE_P(, ChannelTest,
                         // TransportType, Family, Blocking, ErrorRate, Size
                         Values(Param{kTcp, AF_INET, true, 0, 1 << 20},
                                Param{kTcp, AF_INET, false, 0, 1 << 20},
                                Param{kTcp, AF_INET6, true, 0, 1 << 20},
                                Param{kTcp, AF_INET6, false, 0, 1 << 20},
                                Param{kUdp, AF_INET, true, 0, 1 << 10},
                                Param{kUdp, AF_INET, false, 0, 1 << 10},
                                Param{kUdp, AF_INET6, true, 0, 1 << 10},
                                Param{kUdp, AF_INET6, false, 0, 1 << 10},
                                Param{kGrpc, AF_INET, true, 0, 1 << 20},
                                Param{kGrpc, AF_INET6, true, 0, 1 << 20},
                                Param{kMemStream, AF_UNSPEC, true, 0, 1 << 20},
                                Param{kMemStream, AF_UNSPEC, false, 0, 1 << 20},
                                Param{kMemMsg, AF_UNSPEC, true, 0, 1 << 10},
                                Param{kMemMsg, AF_UNSPEC, false, 0, 1 << 10}),
                         ToString);

TEST_P(ChannelTest, BlockingReadWrite) {
  if (!p_.blocking) {
    GTEST_SKIP() << "Skipping test for non-blocking channel.";
    return;
  }

  const auto chs = CreateTestChannelPair(p_);
  Channel* sndr = chs.sndr.get();
  Channel* rcvr = chs.rcvr.get();

  // Precondition: dst is different from src_.
  ASSERT_THAT(dst_, Pointwise(Ne(), src_));

  // Write to one channel.
  IoVecCursor scursor(siovs_);
  EXPECT_EQ(sndr->Write(scursor), size_);

  // Read from the other channel in a different way.
  IoVecCursor dcursor(diovs_);
  EXPECT_EQ(rcvr->Read(dcursor), size_);

  // Shutdown the channels and verify post-shutdown behavior.
  sndr->Shutdown();
  rcvr->Shutdown();
  constexpr size_t kLen = 1;
  IoVecCursor siovs({IoVec(src_.data(), kLen)});
  IoVecCursor diovs({IoVec(dst_.data(), kLen)});
  EXPECT_EQ(sndr->Write(siovs), -1);
  EXPECT_EQ(rcvr->Read(diovs), 0);

  // Check that the data read is the same as written.
  EXPECT_THAT(dst_, Pointwise(Eq(), src_));

  LOG(INFO) << *sndr;
  LOG(INFO) << *rcvr;
}

TEST_P(ChannelTest, NonBlockingReadWrite) {
  if (p_.blocking) {
    GTEST_SKIP() << "Skipping test for blocking channel.";
    return;
  }

  const auto chs = CreateTestChannelPair(p_);
  Channel* sndr = chs.sndr.get();
  Channel* rcvr = chs.rcvr.get();

  // Precondition: dst is different from src_.
  ASSERT_THAT(dst_, Pointwise(Ne(), src_));

  // Use one thread to write to the channel.
  util::Thread writer([&]() {
    const auto poller = CreateEventPoller(*sndr, EPOLLOUT);
    IoVecCursor cursor(siovs_);
    while (cursor.RemainingBytes() > 0) {
      const uint32_t events = WaitForEvents(*poller, *sndr);
      CHECK_NE(events, 0u) << "timed out waiting to write";
      CHECK_EQ(events & (EPOLLERR | EPOLLHUP), 0u) << "socket error";
      const ssize_t n = sndr->Write(cursor);
      CHECK_GE(n, 0) << "write failed";
    }
  });

  // Use another thread to read from the channel.
  util::Thread reader([&]() {
    const auto poller = CreateEventPoller(*rcvr, EPOLLIN | EPOLLRDHUP);
    IoVecCursor cursor(diovs_);
    while (cursor.RemainingBytes() > 0) {
      const uint32_t events = WaitForEvents(*poller, *rcvr);
      CHECK_NE(events, 0u) << "timed out waiting to read";
      const ssize_t n = rcvr->Read(cursor);
      CHECK_GE(n, 0) << "read failed";
      if (n == 0) {
        CHECK_EQ(events & (EPOLLRDHUP | EPOLLHUP | EPOLLERR), 0u)
            << "read failed with events: " << events;
      }
    }
  });

  writer.join();
  reader.join();

  // Check that the data read is the same as written.
  EXPECT_THAT(dst_, Pointwise(Eq(), src_));

  LOG(INFO) << *sndr;
  LOG(INFO) << *rcvr;
}

TEST(UnreliableMessageChannelTest, ErrorRate) {
  const bool kBlocking = true;
  constexpr int kErrorRate = 30;  // percentage
  ConnectedChannelPair mem = CreateMemMsgChannelPair(kBlocking, kErrorRate);
  Channel* sndr = mem.sndr.get();
  Channel* rcvr = mem.rcvr.get();

  constexpr int kNumMessages = 1000;
  constexpr size_t kMsgSize = 128;
  std::vector<Byte> src(kMsgSize, 1);
  std::vector<Byte> sink(kMsgSize, 0);

  int errors = 0;
  for (int i = 0; i < kNumMessages; ++i) {
    IoVecCursor siovs({IoVec(src.data(), kMsgSize)});
    IoVecCursor diovs({IoVec(sink.data(), kMsgSize)});
    ASSERT_EQ(sndr->Write(siovs), kMsgSize);
    if (rcvr->Read(diovs) != kMsgSize) ++errors;
  }
  for (int i = 0; i < kNumMessages; ++i) {
    IoVecCursor siovs({IoVec(src.data(), kMsgSize)});
    IoVecCursor diovs({IoVec(sink.data(), kMsgSize)});
    ASSERT_EQ(sndr->Write(siovs), kMsgSize);
    if (rcvr->Read(diovs) != kMsgSize) ++errors;
  }

  const double actual = 100.0 * errors / (2 * kNumMessages);
  LOG(INFO) << "Actual error rate: " << actual << "%";
  EXPECT_NEAR(actual, kErrorRate, 10.0);
}

// Returns `size` random bytes.
std::vector<Byte> RandomBytes(const size_t size) {
  std::vector<Byte> data(size);
  util::RandomNonZero(absl::MakeSpan(data));
  return data;
}

// Writes the `data` to the channel in a single write call.
ssize_t WriteOnce(Channel& ch, std::vector<Byte>& data) {
  IoVecCursor cursor({IoVec(data.data(), data.size())});
  return ch.Write(cursor);
}

// Reads from the channel into the `buf` in a single read call.
ssize_t ReadOnce(Channel& ch, std::vector<Byte>& buf) {
  IoVecCursor cursor({IoVec(buf.data(), buf.size())});
  return ch.Read(cursor);
}

// Expects that one message of `size` bytes goes from the `sndr` to the `rcvr`.
void ExpectTransfer(Channel& sndr, Channel& rcvr, const size_t size) {
  std::vector<Byte> src = RandomBytes(size);
  std::vector<Byte> dst(size, 0);
  EXPECT_EQ(WriteOnce(sndr, src), size);
  EXPECT_EQ(ReadOnce(rcvr, dst), size);
  EXPECT_TRUE(dst == src);
}

// Expects that the `ch` has been closed by its peer: the read calls return 0,
// and the write calls fail once the channel has learned that the peer is gone,
// which can be after it has accepted a few more messages.
void ExpectClosedByPeer(Channel& ch) {
  std::vector<Byte> data(1, 1);
  EXPECT_EQ(ReadOnce(ch, data), 0);
  const absl::Time deadline = absl::Now() + absl::Seconds(30);
  ssize_t len = 0;
  while (len >= 0 && absl::Now() < deadline) {
    len = WriteOnce(ch, data);
  }
  EXPECT_EQ(len, -1);
}

constexpr absl::Duration kGrpcAcceptTimeout = absl::Seconds(30);

// Creates a gRPC manager listening on a free port of the loopback address.
// The tests use gRPC local credentials, which only allow loopback connections.
std::unique_ptr<GrpcManager> CreateGrpcManager() {
  auto manager = GrpcManager::Create(
      Endpoint::Create("127.0.0.1:0"),
      grpc::experimental::LocalServerCredentials(LOCAL_TCP));
  CHECK_OK(manager.status());
  return std::move(*manager);
}

// Creates the client side of a new gRPC channel to the `manager`.
std::unique_ptr<Channel> ConnectGrpc(const GrpcManager& manager) {
  Endpoint peer = Endpoint::Create("127.0.0.1:0");
  peer.SetPort(static_cast<port_t>(manager.Port()));
  return std::make_unique<GrpcChannel>(GrpcManager::Connect(
      peer, grpc::experimental::LocalCredentials(LOCAL_TCP)));
}

// Waits up to `timeout` for the server side of a gRPC channel connected to the
// `manager`. Returns nullptr if there is none within the `timeout`.
std::unique_ptr<Channel> AcceptGrpc(GrpcManager& manager,
                                    const absl::Duration timeout) {
  std::unique_ptr<GrpcStream> stream = manager.Accept(timeout);
  if (stream == nullptr) return nullptr;
  return std::make_unique<GrpcChannel>(std::move(stream));
}

// Tests one gRPC channel in one direction: the test parameter is true iff the
// sender is the client side of the channel. Both directions are tested because
// the two sides of a gRPC channel are implemented differently.
class GrpcChannelTest : public ::testing::TestWithParam<bool> {
 protected:
  GrpcChannelTest()
      : manager_(CreateGrpcManager()),
        client_(ConnectGrpc(*manager_)),
        server_(AcceptGrpc(*manager_, kGrpcAcceptTimeout)),
        sndr_(GetParam() ? client_ : server_),
        rcvr_(GetParam() ? server_ : client_) {
    CHECK_NE(server_, nullptr);
  }

 protected:
  // The manager is destroyed after the channels.
  const std::unique_ptr<GrpcManager> manager_;
  std::unique_ptr<Channel> client_;
  std::unique_ptr<Channel> server_;
  std::unique_ptr<Channel>& sndr_;
  std::unique_ptr<Channel>& rcvr_;
};

INSTANTIATE_TEST_SUITE_P(, GrpcChannelTest, Values(true, false),
                         [](const ::testing::TestParamInfo<bool>& info) {
                           return info.param ? "ClientToServer"
                                             : "ServerToClient";
                         });

TEST_P(GrpcChannelTest, Properties) {
  EXPECT_EQ(sndr_->Type(), ChannelType::kGRPC);
  EXPECT_TRUE(sndr_->IsBlocking());
  EXPECT_EQ(sndr_->GetFd().value(), -1);
  LOG(INFO) << *sndr_;
}

TEST_P(GrpcChannelTest, ReadWrite) {
  ExpectTransfer(*sndr_, *rcvr_, 1000);
  ExpectTransfer(*rcvr_, *sndr_, 2000);
  ExpectTransfer(*sndr_, *rcvr_, 3000);
}

TEST_P(GrpcChannelTest, KeepsMessageBoundaries) {
  std::vector<std::vector<Byte>> msgs;
  for (const size_t size : {1, 1000, 100'000}) {
    msgs.push_back(RandomBytes(size));
    ASSERT_EQ(WriteOnce(*sndr_, msgs.back()), size);
  }
  // Each read gets exactly one message, though its buffer can hold more.
  for (const std::vector<Byte>& msg : msgs) {
    std::vector<Byte> buf(200'000, 0);
    ASSERT_EQ(ReadOnce(*rcvr_, buf), msg.size());
    buf.resize(msg.size());
    EXPECT_TRUE(buf == msg);
  }
}

TEST_P(GrpcChannelTest, MessageBiggerThanGrpcDefaultLimit) {
  // gRPC rejects a received message of more than 4 MiB by default.
  ExpectTransfer(*sndr_, *rcvr_, 8 << 20);
}

TEST_P(GrpcChannelTest, MessageBiggerThanMaxSizeIsNotWritten) {
  // Reuses one small buffer to describe a message of 2 GiB.
  std::vector<Byte> data(1 << 20, 1);
  const size_t n = GrpcChannel::kMaxMessageSize / data.size() + 1;
  const std::vector<IoVec> iovs(n, IoVec(data.data(), data.size()));
  IoVecCursor cursor(iovs);
  ASSERT_GT(cursor.RemainingBytes(), GrpcChannel::kMaxMessageSize);
  EXPECT_EQ(sndr_->Write(cursor), -1);
  EXPECT_EQ(cursor.RemainingBytes(), cursor.TotalBytes());
}

TEST_P(GrpcChannelTest, MessageBiggerThanReadBufferIsNotRead) {
  std::vector<Byte> src = RandomBytes(100);
  std::vector<Byte> dst(src.size() - 1, 0);
  ASSERT_EQ(WriteOnce(*sndr_, src), src.size());
  EXPECT_EQ(ReadOnce(*rcvr_, dst), -1);
}

TEST_P(GrpcChannelTest, ConcurrentReadWrite) {
  constexpr int kNumWriters = 2;
  constexpr int kNumMessages = 100;
  constexpr size_t kSize = 10'000;
  auto send = [](Channel* ch) {
    std::vector<Byte> src = RandomBytes(kSize);
    for (int i = 0; i < kNumMessages; ++i) {
      CHECK_EQ(WriteOnce(*ch, src), kSize);
    }
  };
  auto recv = [](Channel* ch) {
    std::vector<Byte> dst(kSize, 0);
    for (int i = 0; i < kNumWriters * kNumMessages; ++i) {
      CHECK_EQ(ReadOnce(*ch, dst), kSize);
    }
  };
  // Each side of the channel has one reader and multiple writers at a time.
  std::vector<util::Thread> threads;
  for (Channel* ch : {sndr_.get(), rcvr_.get()}) {
    threads.emplace_back(recv, ch);
    for (int i = 0; i < kNumWriters; ++i) {
      threads.emplace_back(send, ch);
    }
  }
  for (util::Thread& thread : threads) {
    thread.join();
  }
}

TEST_P(GrpcChannelTest, Shutdown) {
  std::vector<Byte> data(1, 1);
  // Shutting down more than once is fine.
  for (int i = 0; i < 2; ++i) {
    sndr_->Shutdown();
    EXPECT_EQ(WriteOnce(*sndr_, data), -1);
    EXPECT_EQ(ReadOnce(*sndr_, data), 0);
  }
  // The peer sees the end of the messages.
  EXPECT_EQ(ReadOnce(*rcvr_, data), 0);
}

TEST_P(GrpcChannelTest, PeerReadsMessageWrittenBeforeShutdown) {
  std::vector<Byte> src = RandomBytes(1000);
  std::vector<Byte> dst(src.size(), 0);
  ASSERT_EQ(WriteOnce(*sndr_, src), src.size());
  sndr_->Shutdown();
  EXPECT_EQ(ReadOnce(*rcvr_, dst), src.size());
  EXPECT_TRUE(dst == src);
  EXPECT_EQ(ReadOnce(*rcvr_, dst), 0);
}

TEST_P(GrpcChannelTest, ShutdownUnblocksRead) {
  // The read blocks because the peer writes nothing.
  util::Thread reader([this]() {
    std::vector<Byte> buf(1, 0);
    CHECK_EQ(ReadOnce(*sndr_, buf), 0);
  });
  // Gives the reader some time to block. The test passes in either order.
  absl::SleepFor(absl::Milliseconds(100));
  sndr_->Shutdown();
  reader.join();
}

TEST_P(GrpcChannelTest, ShutdownUnblocksWrite) {
  // The writes eventually block because the peer reads nothing, so the
  // channel cannot finish sending a message.
  util::Thread writer([this]() {
    std::vector<Byte> src = RandomBytes(1 << 20);
    while (WriteOnce(*sndr_, src) >= 0) {
    }
  });
  // Gives the writer some time to block. The test passes in either order.
  absl::SleepFor(absl::Milliseconds(500));
  sndr_->Shutdown();
  writer.join();
}

TEST_P(GrpcChannelTest, DestroyedPeerClosesChannel) {
  sndr_ = nullptr;
  ExpectClosedByPeer(*rcvr_);
}

TEST_P(GrpcChannelTest, ManagerStopClosesChannels) {
  manager_->Stop();
  ExpectClosedByPeer(*sndr_);
  ExpectClosedByPeer(*rcvr_);
}

}  // namespace
}  // namespace peregrine::internal::testing
