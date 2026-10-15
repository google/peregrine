#include "peregrine/src/internal/chunk/header_flatbuf.h"

#include <cstdint>
#include <string>
#include <vector>

#include "gtest/gtest.h"
#include "peregrine/src/api/transport_types.h"
#include "peregrine/src/internal/chunk/chunk.h"
#include "peregrine/src/internal/chunk/chunk_test_util.h"
#include "peregrine/src/internal/chunk/header.h"

namespace peregrine::internal::testing {
namespace {

TEST(HeaderSerializationTest, PostInfoHeaderSerde) {
  PostInfoHeader info = {};
  info.opcode = Opcode::kPostInfo;
  info.handle = kHandle;
  info.len = 256;
  info.metadata = {1, 2, 3, 4, 5};
  ASSERT_TRUE(info.IsValid());

  const std::string s = HeaderUtil::Serialize(info);
  ASSERT_EQ(s.size(), HeaderUtil::kSize);
  EXPECT_EQ(s[0], 'P');
  EXPECT_EQ(s[1], 'G');
  EXPECT_EQ(static_cast<uint8_t>(s[2]), 1);
  EXPECT_EQ(static_cast<uint8_t>(s[3]),
            static_cast<uint8_t>(Opcode::kPostInfo));

  HeaderBase base = {};
  ASSERT_TRUE(HeaderUtil::Deserialize(s, base));
  EXPECT_TRUE(base.IsPostInfo());
  EXPECT_EQ(base.handle, kHandle);
  EXPECT_EQ(base.len, 256);

  PostInfoHeader output = {};
  ASSERT_TRUE(HeaderUtil::Deserialize(s, output));
  EXPECT_EQ(info, output);
  EXPECT_TRUE(output.IsPostInfo());

  DataHeader wrong_type = {};
  EXPECT_FALSE(HeaderUtil::Deserialize(s, wrong_type));
}

TEST(HeaderSerializationTest, DataAndAckHeaderSerde) {
  DataHeader data = {};
  data.opcode = Opcode::kData;
  data.handle = kHandle;
  data.len = kChunkSize;
  data.reqid = kReqId;
  data.index = kChunkIndex;
  data.byte_offset = 16384;
  data.raddr = kBufferBaseAddr;
  ASSERT_TRUE(data.IsValid());

  const std::string s = HeaderUtil::Serialize(data);
  ASSERT_EQ(s.size(), HeaderUtil::kSize);
  EXPECT_EQ(s[0], 'P');
  EXPECT_EQ(s[1], 'G');
  EXPECT_EQ(static_cast<uint8_t>(s[2]), 1);
  EXPECT_EQ(static_cast<uint8_t>(s[3]), static_cast<uint8_t>(Opcode::kData));

  HeaderBase base = {};
  ASSERT_TRUE(HeaderUtil::Deserialize(s, base));
  EXPECT_TRUE(base.IsData());
  EXPECT_FALSE(base.IsAck());

  DataHeader output = {};
  ASSERT_TRUE(HeaderUtil::Deserialize(s, output));
  EXPECT_EQ(data, output);
  EXPECT_TRUE(output.IsData());
  EXPECT_FALSE(output.IsAck());

  PostInfoHeader wrong_type = {};
  EXPECT_FALSE(HeaderUtil::Deserialize(s, wrong_type));

  DataHeader ack = {};
  ack.opcode = Opcode::kData;
  ack.handle = kHandle;
  ack.len = 0;
  ack.reqid = kReqId;
  ack.index = kChunkIndex;
  const std::string ack_s = HeaderUtil::Serialize(ack);
  ASSERT_EQ(ack_s.size(), HeaderUtil::kSize);

  DataHeader ack_out = {};
  ASSERT_TRUE(HeaderUtil::Deserialize(ack_s, ack_out));
  EXPECT_EQ(ack, ack_out);
  EXPECT_FALSE(ack_out.IsData());
  EXPECT_TRUE(ack_out.IsAck());
}

TEST(HeaderSerializationTest, InvalidHeaderBytes) {
  HeaderBase base_out = {};
  PostInfoHeader info_out = {};
  DataHeader data_out = {};
  EXPECT_FALSE(HeaderUtil::Deserialize("too_short", base_out));
  EXPECT_FALSE(HeaderUtil::Deserialize("too_short", info_out));
  EXPECT_FALSE(HeaderUtil::Deserialize("too_short", data_out));

  std::string bad(HeaderUtil::kSize, '\0');
  EXPECT_FALSE(HeaderUtil::Deserialize(bad, base_out));
  EXPECT_FALSE(HeaderUtil::Deserialize(bad, info_out));
  EXPECT_FALSE(HeaderUtil::Deserialize(bad, data_out));

  PostInfoHeader info = {};
  info.opcode = Opcode::kPostInfo;
  info.handle = kHandle;
  info.len = 256;
  const std::string valid_s = HeaderUtil::Serialize(info);

  std::string bad_ver = valid_s;
  bad_ver[2] = 2;
  EXPECT_FALSE(HeaderUtil::Deserialize(bad_ver, base_out));
  EXPECT_FALSE(HeaderUtil::Deserialize(bad_ver, info_out));

  std::string bad_opcode = valid_s;
  bad_opcode[3] = 99;
  EXPECT_FALSE(HeaderUtil::Deserialize(bad_opcode, base_out));
  EXPECT_FALSE(HeaderUtil::Deserialize(bad_opcode, info_out));
  EXPECT_FALSE(HeaderUtil::Deserialize(bad_opcode, data_out));
}

TEST(RequestInfoSerializationTest, SingleAndBatchSerde) {
  RequestInfo req1 = {};
  req1.reqid = kReqId;
  req1.nchunks = 16;
  req1.raddr = kBufferBaseAddr;
  req1.total_len = 65536;
  req1.op = Op::kWrite;
  req1.recipe.type = RecipeType::kContiguous;
  req1.recipe.params = {10, 20, 30};
  req1.metadata = {'r', 'e', 'q', '1', '\0', 'm', 'e', 't', 'a'};
  ASSERT_TRUE(req1.IsValid());

  const std::string single_s = HeaderUtil::Serialize(req1);
  ASSERT_EQ(single_s.size(), HeaderUtil::kRequestInfoSize);

  RequestInfo single_out = {};
  ASSERT_TRUE(HeaderUtil::Deserialize(single_s, single_out));
  EXPECT_EQ(req1, single_out);

  RequestInfo req2 = {};
  req2.reqid = kReqId2;
  req2.nchunks = 1;
  req2.raddr = kPayloadSrcAddr;
  req2.total_len = 4096;
  req2.op = Op::kRead;
  req2.recipe.type = RecipeType::kContiguous;
  ASSERT_TRUE(req2.IsValid());

  const std::vector<RequestInfo> requests = {req1, req2};
  const std::string batch_s = HeaderUtil::Serialize(requests);
  ASSERT_EQ(batch_s.size(), 2 * HeaderUtil::kRequestInfoSize);

  std::vector<RequestInfo> batch_out;
  ASSERT_TRUE(HeaderUtil::Deserialize(batch_s, batch_out));
  EXPECT_EQ(requests, batch_out);
}

TEST(RequestInfoSerializationTest, EmptyAndCorruptedPayload) {
  RequestInfo single_out = {};
  EXPECT_FALSE(HeaderUtil::Deserialize("", single_out));
  EXPECT_FALSE(HeaderUtil::Deserialize("abc", single_out));
  EXPECT_FALSE(HeaderUtil::Deserialize(
      std::string(HeaderUtil::kRequestInfoSize, '\0'), single_out));

  std::vector<RequestInfo> batch_out;
  EXPECT_FALSE(HeaderUtil::Deserialize("", batch_out));
  EXPECT_FALSE(HeaderUtil::Deserialize("abc", batch_out));
  EXPECT_FALSE(HeaderUtil::Deserialize(
      std::string(HeaderUtil::kRequestInfoSize, '\0'), batch_out));
}

}  // namespace
}  // namespace peregrine::internal::testing
