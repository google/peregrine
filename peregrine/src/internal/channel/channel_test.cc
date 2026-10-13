#include "peregrine/src/internal/channel/channel.h"

#include <sys/epoll.h>
#include <sys/socket.h>
#include <sys/types.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "absl/log/check.h"
#include "absl/log/log.h"
#include "absl/types/span.h"
#include "peregrine/src/api/transport_types.h"
#include "peregrine/src/internal/base/types.h"
#include "peregrine/src/internal/channel/channel_test_util.h"
#include "peregrine/src/internal/event/poller.h"
#include "peregrine/src/internal/lib/iovec_cursor.h"
#include "peregrine/src/internal/util/test_param.h"
#include "peregrine/src/util/thread.h"
#include "peregrine/src/util/util.h"

namespace peregrine::internal::testing {
namespace {

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
  CHECK_EQ(poller->Register(ch.GetFd(), events), 0);
  return poller;
}

uint32_t WaitForEvents(Poller& poller) {
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
                         Values(Param{kTcp, AF_INET, false, 0, 1 << 20},
                                Param{kTcp, AF_INET6, false, 0, 1 << 20},
                                Param{kTcp, AF_INET, true, 0, 1 << 20},
                                Param{kTcp, AF_INET6, true, 0, 1 << 20},
                                Param{kUdp, AF_INET, true, 0, 1 << 10},
                                Param{kUdp, AF_INET6, true, 0, 1 << 10},
                                Param{kMemStream, AF_UNSPEC, true, 0, 1 << 20},
                                Param{kMemMsg, AF_UNSPEC, true, 0, 1 << 10}),
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
  EXPECT_EQ(sndr->Write(siovs_), size_);

  // Read from the other channel in a different way.
  EXPECT_EQ(rcvr->Read(absl::MakeSpan(diovs_)), size_);

  // Shutdown the channels and verify post-shutdown behavior.
  sndr->Shutdown();
  rcvr->Shutdown();
  constexpr size_t kLen = 1;
  IoVec siovs[] = {{src_.data(), kLen}};
  IoVec diovs[] = {{dst_.data(), kLen}};
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
      const uint32_t events = WaitForEvents(*poller);
      CHECK_NE(events, 0u) << "timed out waiting to write";
      CHECK_EQ(events & (EPOLLERR | EPOLLHUP), 0u) << "socket error";
      const ssize_t n = sndr->Write(cursor.RemainingSpan());
      CHECK_GE(n, 0) << "write failed";
      if (n > 0) cursor.Advance(static_cast<size_t>(n));
    }
  });

  // Use another thread to read from the channel.
  util::Thread reader([&]() {
    const auto poller = CreateEventPoller(*rcvr, EPOLLIN | EPOLLRDHUP);
    IoVecCursor cursor(diovs_);
    while (cursor.RemainingBytes() > 0) {
      const uint32_t events = WaitForEvents(*poller);
      CHECK_NE(events, 0u) << "timed out waiting to read";
      const ssize_t n = rcvr->Read(cursor.RemainingSpan());
      CHECK_GE(n, 0) << "read failed";
      if (n > 0) {
        cursor.Advance(static_cast<size_t>(n));
      } else {
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
  constexpr int kErrorRate = 30;  // percentage
  ConnectedChannelPair mem = CreateMemMsgChannelPair(kErrorRate);
  Channel* sndr = mem.sndr.get();
  Channel* rcvr = mem.rcvr.get();

  constexpr int kNumMessages = 1000;
  constexpr size_t kMsgSize = 128;
  std::vector<Byte> src(kMsgSize, 1);
  std::vector<Byte> sink(kMsgSize, 0);
  const IoVec siovs[] = {{src.data(), kMsgSize}};
  IoVec diovs[] = {{sink.data(), kMsgSize}};

  int errors = 0;
  for (int i = 0; i < kNumMessages; ++i) {
    ASSERT_EQ(sndr->Write(siovs), kMsgSize);
    if (rcvr->Read(diovs) != kMsgSize) ++errors;
  }
  for (int i = 0; i < kNumMessages; ++i) {
    ASSERT_EQ(sndr->Write(siovs), kMsgSize);
    if (rcvr->Read(diovs) != kMsgSize) ++errors;
  }

  const double actual = 100.0 * errors / (2 * kNumMessages);
  LOG(INFO) << "Actual error rate: " << actual << "%";
  EXPECT_NEAR(actual, kErrorRate, 10.0);
}

}  // namespace
}  // namespace peregrine::internal::testing
