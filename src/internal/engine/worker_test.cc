#include "src/internal/engine/worker.h"

#include <sys/socket.h>

#include <cstddef>
#include <cstdint>
#include <memory>
#include <thread>  // NOLINT
#include <utility>
#include <vector>

#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "absl/log/check.h"
#include "absl/random/random.h"
#include "absl/synchronization/notification.h"
#include "absl/time/clock.h"
#include "absl/time/time.h"
#include "src/api/transport_types.h"
#include "src/internal/assumptions.h"
#include "src/internal/base/hostinfo.h"
#include "src/internal/base/types.h"
#include "src/internal/channel/channel.h"
#include "src/internal/channel/channel_test_util.h"
#include "src/internal/chunk/chunk.h"
#include "src/internal/request/request_tracker.h"
#include "src/internal/util/test_util.h"
#include "src/util/util.h"

namespace peregrine::internal::testing {
namespace {

using ::testing::Eq;
using ::testing::Ne;
using ::testing::Pointwise;

static_assert(assumptions::kBufferIsDividedIntoFixedSizeChunks);
constexpr Handle kHandle(0x1234);
constexpr ReqId kReqId(0xbeef);
constexpr uint32_t kNumChunks = 1024;
constexpr uint32_t kChunkSize = 64;
constexpr uint32_t kLastChunkSize = kChunkSize - 1;
constexpr size_t kBufSize = (kNumChunks - 1) * kChunkSize + kLastChunkSize;
static_assert(kBufSize % kNumChunks != 0);

class WorkerTest : public ::testing::Test {
 protected:
  WorkerTest()
      : src_(kBufSize),
        dst_(kBufSize),
        chs_(ConnectedChannelPair::CreateTcp(AF_INET6)),
        s_(TestOnly_LocalHostInfo(AF_INET6, /*tcp=*/true)),
        r_(TestOnly_LocalHostInfo(AF_INET6, /*tcp=*/true)),
        sndr_(5, s_, std::move(chs_.sndr)),
        rcvr_(0, r_, std::move(chs_.rcvr)) {
    for (int i = 0; i < kBufSize; ++i) {
      src_[i] = util::Random<Byte>(bitgen_, 0x01, 0xff);
      dst_[i] = Byte(0);
    }
    DCHECK_NE(src_.data(), dst_.data());
  }

  static uint32_t GetChunkSize(uint32_t i) {
    DCHECK_LT(i, kNumChunks);
    return i != kNumChunks - 1 ? kChunkSize : kLastChunkSize;
  }

  ChunkMetadata GenChunk(uint32_t i) {
    static_assert(assumptions::kBufferIsDividedIntoFixedSizeChunks);
    const uint64_t dst_buffer_addr(reinterpret_cast<uint64_t>(dst_.data()));
    const uint64_t offset = static_cast<uint64_t>(i) * kChunkSize;
    const addr_t dst_chunk_addr(dst_buffer_addr + offset);
    return ChunkMetadata{.handle = kHandle,
                         .reqid = kReqId,
                         .nchunks = kNumChunks,
                         .index = chunk_t(i),
                         .addr = dst_chunk_addr,
                         .size = GetChunkSize(i)};
  }

 private:
  struct Host {
    RequestTracker outgoing;
    RequestTracker incoming;
    Worker worker;
    explicit Host(int id, const HostInfo& self,
                  std::unique_ptr<Channel> channel)
        : outgoing(),
          incoming(),
          worker(id, self, outgoing, incoming, std::move(channel)) {}
  };

 protected:
  absl::BitGen bitgen_;
  std::vector<Byte> src_;
  std::vector<Byte> dst_;
  ConnectedChannelPair chs_;
  const HostInfo s_;
  const HostInfo r_;
  Host sndr_;
  Host rcvr_;
};

TEST_F(WorkerTest, SendRecv) {
  // Precondition: dst is different from src.
  ASSERT_THAT(dst_, Pointwise(Ne(), src_));

  absl::Notification done;
  std::jthread s([&]() {
    while (sndr_.outgoing.Check(kHandle) != Status::kSuccess) {
      for (int i = 0; i < kNumChunks; ++i) {
        const uint64_t offset = static_cast<uint64_t>(i) * kChunkSize;
        const Byte* const chunk_src_addr = src_.data() + offset;
        sndr_.worker.EnqueueChunk(chunk_src_addr, GenChunk(i));
      }
      absl::SleepFor(absl::Milliseconds(100));
    }
    done.Notify();
  });

  std::jthread r([&]() {
    while (!done.HasBeenNotified()) {
      absl::SleepFor(absl::Milliseconds(100));
    }
  });

  s.join();
  r.join();

  // Postcondition: dst is the same as src.
  EXPECT_THAT(dst_, Pointwise(Eq(), src_));
}

}  // namespace
}  // namespace peregrine::internal::testing
