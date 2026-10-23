#include "peregrine/src/internal/chunk/header.h"

#include "gtest/gtest.h"
#include "absl/log/log.h"
#include "peregrine/src/api/transport_types.h"
#include "peregrine/src/internal/chunk/chunk.h"
#include "peregrine/src/internal/chunk/chunk_test_util.h"

namespace peregrine::internal::testing {
namespace {

TEST(HeaderTest, PostInfoDataAndAckProperties) {
  PostInfoHeader info = {};
  info.opcode = Opcode::kPostInfo;
  info.handle = kHandle;
  info.len = 128;
  info.metadata[0] = 0x42;
  EXPECT_TRUE(info.IsPostInfo());
  EXPECT_FALSE(info.IsData());
  EXPECT_FALSE(info.IsAck());
  EXPECT_TRUE(info.IsValid());
  EXPECT_TRUE(IsMatch(info, GenPayload(128)));
  EXPECT_EQ(info, info);
  LOG(INFO) << static_cast<const HeaderBase&>(info);
  LOG(INFO) << info;

  info.len = 0;
  EXPECT_FALSE(info.IsValid());

  info.len = 100;
  EXPECT_FALSE(info.IsValid());
  EXPECT_FALSE(static_cast<const HeaderBase&>(info).IsValid());

  DataHeader data = {};
  data.opcode = Opcode::kData;
  data.handle = kHandle;
  data.len = kChunkSize;
  data.reqid = kReqId;
  data.index = kChunkIndex;
  data.byte_offset = 4096;
  data.raddr = kBufferBaseAddr;
  EXPECT_FALSE(data.IsPostInfo());
  EXPECT_TRUE(data.IsData());
  EXPECT_FALSE(data.IsAck());
  EXPECT_TRUE(data.IsValid());
  EXPECT_EQ(data.DstAddr(), reinterpret_cast<Byte*>(kBufferBaseAddr.value()));
  EXPECT_TRUE(IsMatch(data, GenPayload(kChunkSize)));
  EXPECT_FALSE(IsMatch(data, GenPayload(kChunkSize - 1)));
  EXPECT_EQ(data, data);
  LOG(INFO) << data;

  DataHeader ack = {};
  ack.opcode = Opcode::kData;
  ack.handle = kHandle;
  ack.len = 0;
  ack.reqid = kReqId;
  ack.index = kChunkIndex;
  EXPECT_FALSE(ack.IsData());
  EXPECT_TRUE(ack.IsAck());
  EXPECT_TRUE(ack.IsValid());
  EXPECT_TRUE(IsMatch(ack, ChunkPayloadView()));
}

TEST(RequestInfoTest, ValidationAndEquality) {
  RequestInfo req = {};
  EXPECT_FALSE(req.IsValid());

  req.reqid = kReqId;
  req.nchunks = 8;
  req.raddr = kBufferBaseAddr;
  req.total_len = 8192;
  req.op = Op::kWrite;
  req.recipe.type = RecipeType::kContiguous;
  req.recipe.params[0] = 0x11;
  req.metadata[0] = 0x22;
  EXPECT_TRUE(req.IsValid());
  EXPECT_EQ(req, req);
  LOG(INFO) << req;
}

}  // namespace
}  // namespace peregrine::internal::testing
