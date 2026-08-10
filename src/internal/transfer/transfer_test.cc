#include "src/internal/transfer/transfer.h"

#include <sys/socket.h>

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <memory>
#include <string>
#include <thread>  // NOLINT
#include <vector>

#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "absl/log/check.h"
#include "absl/log/log.h"
#include "absl/random/random.h"
#include "src/api/transport_types.h"
#include "src/internal/assumptions.h"
#include "src/internal/base/types.h"
#include "src/internal/channel/channel.h"
#include "src/internal/channel/channel_test_util.h"
#include "src/internal/chunk/chunk.h"
#include "src/internal/request/request_tracker.h"
#include "src/internal/util/test_param.h"
#include "src/util/util.h"

namespace peregrine::internal::testing {
namespace {

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

using Param = TestChannelErrorParam;

std::string ToString(const TestParamInfo<Param>& info) {
  return testing::ToString(info.param);
}

class TransferTest : public ::testing::TestWithParam<Param> {
 protected:
  TransferTest() : src_(kBufSize), dst_(kBufSize), a_(), b_() {
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
    RequestTracker outgoing;
    RequestTracker incoming;
    Host() : outgoing(), incoming() {}
  };

 protected:
  absl::BitGen bitgen_;
  std::vector<Byte> src_;
  std::vector<Byte> dst_;
  Host a_;
  Host b_;
};

INSTANTIATE_TEST_SUITE_P(
    , TransferTest,
    Values(Param{TestChannelType::kTcp, AF_INET, /*error_rate=*/0},
           Param{TestChannelType::kTcp, AF_INET6, /*error_rate=*/0},
           Param{TestChannelType::kUdp, AF_INET, /*error_rate=*/0},
           Param{TestChannelType::kUdp, AF_INET6, /*error_rate=*/0},
           Param{TestChannelType::kMemStream, /*family=*/0, /*error_rate=*/0},
           Param{TestChannelType::kMemStream, /*family=*/0, /*error_rate=*/10},
           Param{TestChannelType::kMemStream, /*family=*/0, /*error_rate=*/50},
           Param{TestChannelType::kMemMsg, /*family=*/0, /*error_rate=*/0},
           Param{TestChannelType::kMemMsg, /*family=*/0, /*error_rate=*/10},
           Param{TestChannelType::kMemMsg, /*family=*/0, /*error_rate=*/50}),
    ToString);

TEST_P(TransferTest, SendRecv) {
  const auto p = GetParam();
  const auto chs = CreateTestChannelPair(p.type, p.family, p.error_rate);

  // Precondition: dst is different from src.
  ASSERT_THAT(dst_, Pointwise(Ne(), src_));

  // A sends data chunks to B.
  std::thread a_send([&]() {
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
  std::thread a_recv([&]() {
    Channel* const channel = chs.sndr.get();
    while (!IsSendDone()) {
      if (!Transfer::RecvChunk(channel, a_.outgoing, a_.incoming)) {
        std::this_thread::yield();
      }
    }
  });

  // B receives data chunks from A (and sends ack chunks to A).
  std::thread b_recv([&]() {
    Channel* const channel = chs.rcvr.get();
    while (!IsSendDone()) {
      if (!Transfer::RecvChunk(channel, b_.outgoing, b_.incoming)) {
        std::this_thread::yield();
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
