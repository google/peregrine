#include "src/internal/transfer/transfer.h"

#include <sys/socket.h>

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "absl/log/check.h"
#include "absl/log/log.h"
#include "absl/random/random.h"
#include "absl/time/clock.h"
#include "src/api/transport_types.h"
#include "src/internal/assumptions.h"
#include "src/internal/base/types.h"
#include "src/internal/channel/channel.h"
#include "src/internal/channel/channel_test_util.h"
#include "src/internal/chunk/chunk.h"
#include "src/internal/metrics/engine_metrics.h"
#include "src/internal/request/request_tracker.h"
#include "src/internal/util/test_param.h"
#include "src/util/thread.h"
#include "src/util/util.h"

namespace peregrine::internal::testing {
namespace {

using TestChannelType::kMemMsg;
using TestChannelType::kMemStream;
using TestChannelType::kTcp;
using TestChannelType::kUdp;
using ::testing::Eq;
using ::testing::Ne;
using ::testing::Pointwise;
using ::testing::TestParamInfo;
using ::testing::Values;

static_assert(assumptions::kBufferIsDividedIntoFixedSizeChunks);
constexpr Handle kHandle(0x1234);
constexpr ReqId kReqId(0xbeef);
constexpr uint32_t kNumChunks = 100;
constexpr uint32_t kChunkSize = 16;
constexpr uint32_t kLastChunkSize = kChunkSize - 1;
constexpr size_t kBufSize = (kNumChunks - 1) * kChunkSize + kLastChunkSize;
static_assert(kBufSize % kNumChunks != 0);

using Param = ChannelTestParam;

std::string ToString(const ::testing::TestParamInfo<Param>& info) {
  return info.param.ToString();
}

class TransferTest : public ::testing::TestWithParam<Param> {
 protected:
  TransferTest() : p_(GetParam()), src_(kBufSize), dst_(kBufSize), a_(), b_() {
    for (int i = 0; i < kBufSize; ++i) {
      src_[i] = util::Random<Byte>(bitgen_, 0x01, 0xff);
      dst_[i] = static_cast<Byte>(0);
    }
    DCHECK_NE(src_.data(), dst_.data());
  }

  static uint32_t GetChunkSize(uint32_t i) {
    DCHECK_LT(i, kNumChunks);
    return i != kNumChunks - 1 ? kChunkSize : kLastChunkSize;
  }

  ChunkHeader GenChunkHeader(uint32_t i) {
    DCHECK_LT(i, kNumChunks);
    return ChunkHeader{
        .handle = kHandle,
        .reqid = kReqId,
        .nchunks = kNumChunks,
        .index = chunk_t(i),
        .addr =
            addr_t(reinterpret_cast<uintptr_t>(dst_.data() + i * kChunkSize)),
        .size = GetChunkSize(i)};
  }

  ChunkPayloadView GenPayload(uint32_t i) {
    return ChunkPayloadView(src_.data() + i * kChunkSize, GetChunkSize(i));
  }

  bool IsSendDone() const {
    return a_.outgoing.Check(kHandle) == Status::kSuccess;
  }

  bool IsRecvDone() const {
    return b_.incoming.Check(kHandle) == Status::kSuccess;
  }

 private:
  struct Host {
    EngineMetrics metrics;
    RequestTracker outgoing;
    RequestTracker incoming;
    Host() : metrics(), outgoing(metrics), incoming(metrics) {}
  };

 protected:
  const Param p_;
  absl::BitGen bitgen_;
  std::vector<Byte> src_;
  std::vector<Byte> dst_;
  Host a_;
  Host b_;
};

INSTANTIATE_TEST_SUITE_P(
    , TransferTest,
    Values(Param{kTcp, AF_INET, /*error_rate=*/0, /*size=*/0},
           Param{kTcp, AF_INET6, /*error_rate=*/0, /*size=*/0},
           Param{kUdp, AF_INET, /*error_rate=*/0, /*size=*/0},
           Param{kUdp, AF_INET6, /*error_rate=*/0, /*size=*/0},
           Param{kMemStream, AF_UNSPEC, /*error_rate=*/0, /*size=*/0},
           Param{kMemStream, AF_UNSPEC, /*error_rate=*/10, /*size=*/0},
           Param{kMemStream, AF_UNSPEC, /*error_rate=*/50, /*size=*/0},
           Param{kMemMsg, AF_UNSPEC, /*error_rate=*/0, /*size=*/0},
           Param{kMemMsg, AF_UNSPEC, /*error_rate=*/10, /*size=*/0},
           Param{kMemMsg, AF_UNSPEC, /*error_rate=*/50, /*size=*/0}),
    ToString);

TEST_P(TransferTest, SendRecv) {
  const auto chs = CreateTestChannelPair(p_.type, p_.family, /*blocking=*/true,
                                         p_.error_rate);

  // Precondition: dst is different from src.
  ASSERT_THAT(dst_, Pointwise(Ne(), src_));
  RequestTracker::OnComplete on_complete = nullptr;
  ASSERT_TRUE(
      a_.outgoing.Add(kHandle, {kReqId}, absl::Now(), std::move(on_complete)));

  // A sends data chunks to B.
  util::Thread a_send([&]() {
    Channel* const channel = chs.sndr.get();
    while (!IsSendDone()) {
      for (uint32_t i = 0; i < kNumChunks; ++i) {
        if (IsSendDone()) break;
        const ChunkHeader chunk = GenChunkHeader(i);
        const ChunkPayloadView payload = GenPayload(i);
        CHECK(Transfer::SendChunk(channel, chunk, payload));
      }
    }
  });
  // A receives ack chunks from B.
  util::Thread a_recv([&]() {
    Channel* const channel = chs.sndr.get();
    while (!IsSendDone()) {
      if (!Transfer::RecvChunk(channel, a_.outgoing, a_.incoming)) {
        util::Yield();
      }
    }
  });

  // B receives data chunks from A (and sends ack chunks to A).
  util::Thread b_recv([&]() {
    Channel* const channel = chs.rcvr.get();
    while (!IsSendDone()) {
      if (!Transfer::RecvChunk(channel, b_.outgoing, b_.incoming)) {
        util::Yield();
      }
    }
  });

  a_send.join();
  a_recv.join();
  chs.sndr->Shutdown();
  chs.rcvr->Shutdown();
  b_recv.join();

  EXPECT_TRUE(IsSendDone());
  EXPECT_TRUE(IsRecvDone());

  // Postcondition: dst is the same as src.
  EXPECT_THAT(dst_, Pointwise(Eq(), src_));
}

}  // namespace
}  // namespace peregrine::internal::testing
