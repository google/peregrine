#include "peregrine/src/internal/chunk/header_flatbuf.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "gtest/gtest.h"
#include "absl/types/span.h"
#include "peregrine/src/api/transport_types.h"
#include "peregrine/src/internal/base/endpoint.h"
#include "peregrine/src/internal/base/types.h"
#include "peregrine/src/internal/chunk/chunk_test_util.h"
#include "peregrine/src/internal/chunk/header.h"
#include "peregrine/src/internal/trace/chunk_trace_wire.h"

namespace peregrine::internal::testing {
namespace {

TEST(HeaderSerializationTest, PostInfoHeaderSerde) {
  PostInfoHeader info = {};
  info.opcode = Opcode::kPostInfo;
  info.handle = kHandle;
  info.len = 256;
  info.peer = Endpoint::Create("192.168.1.42:5678");
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
  EXPECT_EQ(output.peer, Endpoint::Create("192.168.1.42:5678"));
  EXPECT_TRUE(output.IsPostInfo());

  PostInfoHeader info_v6 = info;
  info_v6.peer = Endpoint::Create("[2001:db8::1]:9090");
  const std::string s_v6 = HeaderUtil::Serialize(info_v6);
  PostInfoHeader output_v6 = {};
  ASSERT_TRUE(HeaderUtil::Deserialize(s_v6, output_v6));
  EXPECT_EQ(info_v6, output_v6);
  EXPECT_EQ(output_v6.peer, Endpoint::Create("[2001:db8::1]:9090"));

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

TEST(HeaderSerializationTest, FailureHeaderSerde) {
  FailureHeader failure = {};
  failure.opcode = Opcode::kFailure;
  failure.handle = kHandle;
  failure.len = 0;
  ASSERT_TRUE(failure.IsValid());

  const std::string s = HeaderUtil::Serialize(failure);
  ASSERT_EQ(s.size(), HeaderUtil::kSize);
  EXPECT_EQ(s[0], 'P');
  EXPECT_EQ(s[1], 'G');
  EXPECT_EQ(static_cast<uint8_t>(s[2]), 1);
  EXPECT_EQ(static_cast<uint8_t>(s[3]), static_cast<uint8_t>(Opcode::kFailure));

  HeaderBase base = {};
  ASSERT_TRUE(HeaderUtil::Deserialize(s, base));
  EXPECT_TRUE(base.IsFailure());
  EXPECT_EQ(base.handle, kHandle);
  EXPECT_EQ(base.len, 0);

  FailureHeader output = {};
  ASSERT_TRUE(HeaderUtil::Deserialize(s, output));
  EXPECT_EQ(failure, output);
  EXPECT_TRUE(output.IsFailure());

  DataHeader wrong_data = {};
  EXPECT_FALSE(HeaderUtil::Deserialize(s, wrong_data));
  PostInfoHeader wrong_info = {};
  EXPECT_FALSE(HeaderUtil::Deserialize(s, wrong_info));
}

TEST(HeaderSerializationTest, DataAndAckHeaderSerdeWithTrace) {
  DataHeader data = {};
  data.opcode = Opcode::kData;
  data.handle = kHandle;
  data.len = kChunkSize;
  data.reqid = kReqId;
  data.index = kChunkIndex;
  data.byte_offset = 16384;
  data.raddr = kBufferBaseAddr;
  data.trace.tx_send_us = 123456789ULL;
  data.trace.rx_payload_us = 450U;
  data.trace.rx_acquire_us = 12U;
  data.trace.rx_worker_id = -7;
  data.trace.rx_chunk_status = 2U;
  ASSERT_TRUE(data.IsValid());
  ASSERT_TRUE(data.trace.IsTraced());

  const std::string s = HeaderUtil::Serialize(data);
  ASSERT_EQ(s.size(), HeaderUtil::kSize);

  DataHeader output = {};
  ASSERT_TRUE(HeaderUtil::Deserialize(s, output));
  EXPECT_EQ(data, output);
  EXPECT_EQ(output.trace.tx_send_us, 123456789ULL);
  EXPECT_EQ(output.trace.rx_payload_us, 450U);
  EXPECT_EQ(output.trace.rx_acquire_us, 12U);
  EXPECT_EQ(output.trace.rx_worker_id, -7);
  EXPECT_EQ(output.trace.rx_chunk_status, 2U);
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

TEST(HeaderSerializationTest, SerializeIntoSpanMatchesStringOverloads) {
  std::array<Byte, HeaderUtil::kSize> buf = {};
  const auto as_view = [&buf]() {
    return std::string_view(reinterpret_cast<const char*>(buf.data()),
                            buf.size());
  };

  PostInfoHeader info = {};
  info.opcode = Opcode::kPostInfo;
  info.handle = kHandle;
  info.len = 256;
  info.peer = Endpoint::Create("192.168.1.42:5678");
  info.metadata = {1, 2, 3, 4, 5};
  HeaderUtil::Serialize(info, buf);
  EXPECT_EQ(as_view(), HeaderUtil::Serialize(info));

  DataHeader data = {};
  data.opcode = Opcode::kData;
  data.handle = kHandle;
  data.len = kChunkSize;
  data.reqid = kReqId;
  data.index = kChunkIndex;
  data.byte_offset = 16384;
  data.raddr = kBufferBaseAddr;
  HeaderUtil::Serialize(data, buf);
  EXPECT_EQ(as_view(), HeaderUtil::Serialize(data));

  DataHeader ack = data;
  ack.len = 0;
  HeaderUtil::Serialize(ack, buf);
  EXPECT_EQ(as_view(), HeaderUtil::Serialize(ack));

  FailureHeader failure = {};
  failure.opcode = Opcode::kFailure;
  failure.handle = kHandle;
  failure.len = 0;
  HeaderUtil::Serialize(failure, buf);
  EXPECT_EQ(as_view(), HeaderUtil::Serialize(failure));

  RequestInfo req = {};
  req.reqid = kReqId;
  req.nchunks = 4;
  req.raddr = kBufferBaseAddr;
  req.total_len = 4096;
  req.op = Op::kWrite;
  req.recipe.type = RecipeType::kContiguous;
  req.metadata = {'a', 'b', 'c'};
  const std::vector<RequestInfo> reqs = {req, req};
  std::vector<Byte> req_buf(reqs.size() * HeaderUtil::kRequestInfoSize);
  ASSERT_TRUE(HeaderUtil::Serialize(reqs, absl::MakeSpan(req_buf)));
  EXPECT_EQ(std::string_view(reinterpret_cast<const char*>(req_buf.data()),
                             req_buf.size()),
            HeaderUtil::Serialize(reqs));

  info.len = static_cast<uint32_t>(req_buf.size());
  const std::optional<size_t> frame_size =
      HeaderUtil::PostInfoFrameSize(reqs.size());
  ASSERT_TRUE(frame_size.has_value());
  std::vector<Byte> post_frame(*frame_size);
  ASSERT_TRUE(HeaderUtil::SerializePostInfo(
      info.handle, info.peer, info.metadata, reqs, absl::MakeSpan(post_frame)));
  EXPECT_EQ(std::string_view(reinterpret_cast<const char*>(post_frame.data()),
                             post_frame.size()),
            HeaderUtil::Serialize(info) + HeaderUtil::Serialize(reqs));
}

TEST(HeaderSerializationTest, RejectsInvalidPostInfoBoundsAndBufferSizes) {
  PostInfoHeader info = {};
  info.opcode = Opcode::kPostInfo;
  info.handle = kHandle;
  info.len = HeaderUtil::kRequestInfoSize;

  RequestInfo req = {};
  req.reqid = kReqId;
  req.nchunks = 1;
  req.total_len = 64;
  req.op = Op::kWrite;
  req.recipe.type = RecipeType::kContiguous;
  const std::vector<RequestInfo> reqs = {req};

  std::vector<Byte> short_reqs(HeaderUtil::kRequestInfoSize - 1);
  EXPECT_FALSE(HeaderUtil::Serialize(reqs, absl::MakeSpan(short_reqs)));
  EXPECT_FALSE(HeaderUtil::Serialize(absl::Span<const RequestInfo>(),
                                     absl::MakeSpan(short_reqs)));

  const std::optional<size_t> one_req_frame = HeaderUtil::PostInfoFrameSize(1);
  ASSERT_TRUE(one_req_frame.has_value());
  std::vector<Byte> short_frame(*one_req_frame - 1);
  EXPECT_FALSE(HeaderUtil::SerializePostInfo(info.handle, info.peer,
                                             info.metadata, reqs,
                                             absl::MakeSpan(short_frame)));
  EXPECT_FALSE(HeaderUtil::SerializePostInfo(
      info.handle, info.peer, info.metadata, absl::Span<const RequestInfo>(),
      absl::MakeSpan(short_frame)));

  const size_t max_reqs = PostInfoHeader::kMaxRequests;
  EXPECT_EQ(HeaderUtil::PostInfoFrameSize(0), std::nullopt);
  EXPECT_EQ(HeaderUtil::PostInfoFrameSize(max_reqs),
            HeaderUtil::kSize + max_reqs * HeaderUtil::kRequestInfoSize);
  EXPECT_EQ(HeaderUtil::PostInfoFrameSize(max_reqs + 1), std::nullopt);

  const absl::Span<const RequestInfo> oversized(&req, max_reqs + 1);
  EXPECT_FALSE(HeaderUtil::Serialize(oversized, absl::MakeSpan(short_reqs)));
  EXPECT_FALSE(HeaderUtil::SerializePostInfo(info.handle, info.peer,
                                             info.metadata, oversized,
                                             absl::MakeSpan(short_frame)));
}

}  // namespace
}  // namespace peregrine::internal::testing
