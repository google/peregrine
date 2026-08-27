#include "src/internal/channel/channel_rdma.h"

#include <cstdint>
#include <cstring>
#include <memory>
#include <string>
#include <vector>

#include "gtest/gtest.h"
#include "absl/types/span.h"
#include "src/api/transport_types.h"
#include "src/internal/base/types.h"
#include "src/internal/channel/channel_types.h"
#include "src/internal/chunk/chunk.h"
#include "src/internal/chunk/chunk_flatbuf.h"
#include "src/internal/rdma/rdma_device_context.h"
#include "src/internal/rdma/rdma_device_manager.h"
#include "src/internal/rdma/rdma_memory_manager.h"
#include "src/internal/rdma/rdma_queue_pair.h"

namespace peregrine::internal {
namespace {

constexpr size_t kBufSize = 64 * 1024;  // 64 KiB

TEST(RdmaChannelTest, LoopbackWrite) {
  auto dev_mgr = RdmaDeviceManager::Create();
  if (!dev_mgr.ok() || dev_mgr.value()->Devices().empty()) {
    GTEST_SKIP() << "No RDMA hardware devices found on this host.";
  }

  RdmaDeviceContext* const dev_ctx = dev_mgr.value()->Devices()[0].get();
  ASSERT_NE(dev_ctx, nullptr);

  RdmaMemoryManager mem_mgr(dev_mgr.value().get());

  // Allocate source and destination test buffers.
  std::vector<uint8_t> src_buf(kBufSize);
  std::vector<uint8_t> dst_buf(kBufSize, 0);

  for (size_t i = 0; i < kBufSize; ++i) {
    src_buf[i] = static_cast<uint8_t>((i * 7 + 13) & 0xFF);
  }

  // Register buffers with RDMA hardware.
  ASSERT_TRUE(mem_mgr.RegisterMemory(src_buf.data(), kBufSize).ok());
  ASSERT_TRUE(mem_mgr.RegisterMemory(dst_buf.data(), kBufSize).ok());

  // Create two Queue Pairs on the same device for loopback testing.
  auto qp_sender = RdmaQueuePair::Create(dev_ctx);
  ASSERT_TRUE(qp_sender.ok());
  auto qp_receiver = RdmaQueuePair::Create(dev_ctx);
  ASSERT_TRUE(qp_receiver.ok());

  // Connect QPs in loopback.
  ASSERT_TRUE(qp_sender.value()
                  ->Connect(qp_receiver.value()->Qpn(), dev_ctx->LocalGid())
                  .ok());
  ASSERT_TRUE(qp_receiver.value()
                  ->Connect(qp_sender.value()->Qpn(), dev_ctx->LocalGid())
                  .ok());

  EXPECT_TRUE(qp_sender.value()->IsConnected());
  EXPECT_TRUE(qp_receiver.value()->IsConnected());

  // Resolve local LKey for source buffer and remote RKey for destination
  // buffer.
  const auto lkey = mem_mgr.GetLKey(src_buf.data(), kBufSize, dev_ctx->Name());
  ASSERT_TRUE(lkey.ok());
  const auto rkey = mem_mgr.GetRKey(dst_buf.data(), kBufSize, dev_ctx->Name());
  ASSERT_TRUE(rkey.ok());

  // Instantiate channel.
  RdmaChannel sender_ch(std::move(qp_sender.value()), lkey.value(),
                        rkey.value());

  EXPECT_EQ(sender_ch.Type(), ChannelType::kReliableMessage);

  // Verify unsupported operations return -1.
  EXPECT_EQ(sender_ch.Write(src_buf.data(), kBufSize), -1);
  EXPECT_EQ(sender_ch.Read(dst_buf.data(), kBufSize), -1);
  EXPECT_EQ(sender_ch.WriteV({}), -1);
  EXPECT_EQ(sender_ch.WriteV({{src_buf.data(), kBufSize}}), -1);

  // Construct ChunkHeader pointing to destination memory address.
  ChunkHeader chunk = {};
  chunk.handle = Handle(1);
  chunk.reqid = ReqId(100);
  chunk.nchunks = 1;
  chunk.index = chunk_t(0);
  chunk.addr = addr_t(reinterpret_cast<uintptr_t>(dst_buf.data()));
  chunk.size = kBufSize;

  const std::string serialized_hdr = ChunkUtil::Serialize(chunk);
  const std::array<const IoVec, 2> iovecs = {
      IoVec(const_cast<char*>(serialized_hdr.data()), serialized_hdr.size()),
      IoVec(src_buf.data(), src_buf.size()),
  };

  // Perform one-sided RDMA Write.
  const ssize_t written = sender_ch.WriteV(iovecs);
  EXPECT_EQ(written, serialized_hdr.size() + kBufSize);

  // Verify that destination buffer contains exact source data.
  EXPECT_EQ(std::memcmp(src_buf.data(), dst_buf.data(), kBufSize), 0);

  // Clean up memory registrations.
  EXPECT_TRUE(mem_mgr.DeregisterMemory(src_buf.data()).ok());
  EXPECT_TRUE(mem_mgr.DeregisterMemory(dst_buf.data()).ok());
}

}  // namespace
}  // namespace peregrine::internal
