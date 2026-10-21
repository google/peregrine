#include "peregrine/src/internal/chunk/header_flatbuf.h"

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <string>
#include <string_view>
#include <vector>

#include "absl/log/check.h"
#include "absl/log/log.h"
#include "absl/types/span.h"
#include "peregrine/src/api/transport_types.h"
#include "peregrine/src/internal/chunk/chunk.h"
#include "peregrine/src/internal/chunk/header.h"
#include "peregrine/src/internal/chunk/header_generated.h"
#include "peregrine/src/internal/trace/chunk_trace_wire.h"

namespace peregrine::internal {

std::string HeaderUtil::Serialize(const PostInfoHeader& header, uint8_t ver) {
  DCHECK_EQ(ver, 1);
  const std::string s = serializeV1(header);
  DCHECK_EQ(s.size(), kSize);
  return s;
}

std::string HeaderUtil::Serialize(const DataHeader& header, uint8_t ver) {
  DCHECK_EQ(ver, 1);
  const std::string s = serializeV1(header);
  DCHECK_EQ(s.size(), kSize);
  return s;
}

bool HeaderUtil::Deserialize(std::string_view s, HeaderBase& base) {
  if (s.size() != kSize) {
    return false;
  }

  alignas(alignof(flatbuf::HeaderBase))
      uint8_t buf[sizeof(flatbuf::HeaderBase)];
  std::memcpy(buf, s.data(), sizeof(buf));

  const auto& fb_base = *reinterpret_cast<const flatbuf::HeaderBase*>(buf);
  if (fb_base.magic() != kMagic) {
    return false;
  }

  const uint8_t ver = fb_base.ver();
  switch (ver) {
    case 1:
      return deserializeV1(fb_base, base);
    default:
      LOG(ERROR) << "Unsupported header flatbuf version: "
                 << static_cast<int>(ver);
      return false;
  }
}

bool HeaderUtil::Deserialize(std::string_view s, PostInfoHeader& header) {
  if (s.size() != kSize) {
    return false;
  }

  alignas(alignof(flatbuf::PostInfoHeader)) uint8_t buf[kSize];
  std::memcpy(buf, s.data(), kSize);

  const auto& h = *reinterpret_cast<const flatbuf::PostInfoHeader*>(buf);
  if (h.base().magic() != kMagic) {
    return false;
  }

  const uint8_t ver = h.base().ver();
  switch (ver) {
    case 1:
      return deserializeV1(h, header);
    default:
      LOG(ERROR) << "Unsupported header flatbuf version: "
                 << static_cast<int>(ver);
      return false;
  }
}

bool HeaderUtil::Deserialize(std::string_view s, DataHeader& header) {
  if (s.size() != kSize) {
    return false;
  }

  alignas(alignof(flatbuf::DataHeader)) uint8_t buf[kSize];
  std::memcpy(buf, s.data(), kSize);

  const auto& h = *reinterpret_cast<const flatbuf::DataHeader*>(buf);
  if (h.base().magic() != kMagic) {
    return false;
  }

  const uint8_t ver = h.base().ver();
  switch (ver) {
    case 1:
      return deserializeV1(h, header);
    default:
      LOG(ERROR) << "Unsupported header flatbuf version: "
                 << static_cast<int>(ver);
      return false;
  }
}

std::string HeaderUtil::Serialize(const RequestInfo& req_info) {
  DCHECK(req_info.IsValid());
  const flatbuf::Recipe fb_recipe(
      static_cast<flatbuf::RecipeType>(req_info.recipe.type),
      /*paddings=*/0, 0, 0, req_info.recipe.params);

  const flatbuf::RequestInfo fb_req(
      req_info.reqid.value(), req_info.nchunks, req_info.raddr.value(),
      req_info.total_len, static_cast<uint8_t>(req_info.op),
      /*paddings=*/0, 0, 0, fb_recipe, req_info.metadata, /*paddings=*/0, 0, 0);
  DCHECK_EQ(sizeof(fb_req), kRequestInfoSize);
  return std::string(reinterpret_cast<const char*>(&fb_req), sizeof(fb_req));
}

bool HeaderUtil::Deserialize(std::string_view s, RequestInfo& req_info) {
  if (s.size() != kRequestInfoSize) {
    return false;
  }

  alignas(alignof(flatbuf::RequestInfo)) uint8_t buf[kRequestInfoSize];
  std::memcpy(buf, s.data(), kRequestInfoSize);

  const auto& fb_req = *reinterpret_cast<const flatbuf::RequestInfo*>(buf);
  req_info.reqid = ReqId(fb_req.reqid());
  req_info.nchunks = fb_req.nchunks();
  req_info.raddr = addr_t(fb_req.raddr());
  req_info.total_len = fb_req.total_len();
  req_info.op = static_cast<Op>(fb_req.op());
  req_info.recipe.type = static_cast<RecipeType>(fb_req.recipe().type());
  if (!req_info.IsValid()) {
    return false;
  }
  std::memcpy(req_info.recipe.params.data(), fb_req.recipe().params()->Data(),
              req_info.recipe.params.size());
  std::memcpy(req_info.metadata.data(), fb_req.metadata()->Data(),
              req_info.metadata.size());
  return true;
}

std::string HeaderUtil::Serialize(absl::Span<const RequestInfo> requests) {
  DCHECK(!requests.empty());
  std::string out;
  out.reserve(requests.size() * kRequestInfoSize);
  for (const RequestInfo& req : requests) {
    DCHECK(req.IsValid());
    const flatbuf::Recipe fb_recipe(
        static_cast<flatbuf::RecipeType>(req.recipe.type),
        /*paddings=*/0, 0, 0, req.recipe.params);
    const flatbuf::RequestInfo fb_req(
        req.reqid.value(), req.nchunks, req.raddr.value(), req.total_len,
        static_cast<uint8_t>(req.op),
        /*paddings=*/0, 0, 0, fb_recipe, req.metadata, /*paddings=*/0, 0, 0);
    out.append(reinterpret_cast<const char*>(&fb_req), sizeof(fb_req));
  }
  DCHECK_EQ(out.size(), requests.size() * kRequestInfoSize);
  return out;
}

bool HeaderUtil::Deserialize(std::string_view s,
                             std::vector<RequestInfo>& requests) {
  if (s.empty() || s.size() % kRequestInfoSize != 0) {
    return false;
  }

  const size_t count = s.size() / kRequestInfoSize;
  requests.resize(count);
  for (size_t i = 0; i < count; ++i) {
    if (!Deserialize(s.substr(i * kRequestInfoSize, kRequestInfoSize),
                     requests[i])) {
      return false;
    }
  }
  return true;
}

std::string HeaderUtil::serializeV1(const PostInfoHeader& header) {
  DCHECK(header.IsValid());
  constexpr uint8_t kVer = 1;
  const flatbuf::HeaderBase base(kMagic, kVer,
                                 static_cast<uint8_t>(header.opcode),
                                 header.handle.value(), header.len);
  const flatbuf::PostInfoHeader h(base, header.metadata, /*paddings=*/0, 0, 0);
  DCHECK_EQ(sizeof(h), kSize);
  return std::string(reinterpret_cast<const char*>(&h), sizeof(h));
}

std::string HeaderUtil::serializeV1(const DataHeader& header) {
  DCHECK(header.IsValid());
  constexpr uint8_t kVer = 1;
  const flatbuf::HeaderBase base(kMagic, kVer,
                                 static_cast<uint8_t>(header.opcode),
                                 header.handle.value(), header.len);
  const flatbuf::ChunkTraceWire trace(
      header.trace.tx_send_mono_us, header.trace.rx_payload_us,
      header.trace.rx_acquire_us, header.trace.rx_worker_id,
      header.trace.rx_chunk_status);
  const flatbuf::DataHeader h(base, header.reqid.value(), header.index.value(),
                              /*padding0=*/0, header.byte_offset,
                              header.raddr.value(), trace,
                              /*padding1=*/0);
  DCHECK_EQ(sizeof(h), kSize);
  return std::string(reinterpret_cast<const char*>(&h), sizeof(h));
}

bool HeaderUtil::deserializeV1(const flatbuf::HeaderBase& fb_base,
                               HeaderBase& base) {
  DCHECK_EQ(fb_base.ver(), 1);
  base.opcode = static_cast<Opcode>(fb_base.opcode());
  base.handle = Handle(fb_base.handle());
  base.len = fb_base.len();
  return base.IsValid();
}

bool HeaderUtil::deserializeV1(const flatbuf::PostInfoHeader& h,
                               PostInfoHeader& header) {
  DCHECK_EQ(h.base().ver(), 1);
  header.opcode = static_cast<Opcode>(h.base().opcode());
  header.handle = Handle(h.base().handle());
  header.len = h.base().len();
  if (!header.IsValid()) {
    return false;
  }
  std::memcpy(header.metadata.data(), h.metadata()->Data(),
              header.metadata.size());
  return true;
}

bool HeaderUtil::deserializeV1(const flatbuf::DataHeader& h,
                               DataHeader& header) {
  DCHECK_EQ(h.base().ver(), 1);
  header.opcode = static_cast<Opcode>(h.base().opcode());
  header.handle = Handle(h.base().handle());
  header.len = h.base().len();
  if (!header.IsValid()) {
    return false;
  }
  header.reqid = ReqId(h.reqid());
  header.index = chunk_t(h.index());
  header.byte_offset = h.byte_offset();
  header.raddr = addr_t(h.raddr());
  const flatbuf::ChunkTraceWire& t = h.trace();
  header.trace.tx_send_mono_us = t.tx_send_mono_us();
  header.trace.rx_payload_us = t.rx_payload_us();
  header.trace.rx_acquire_us = t.rx_acquire_us();
  header.trace.rx_worker_id = t.rx_worker_id();
  header.trace.rx_chunk_status = t.rx_chunk_status();
  return true;
}

}  // namespace peregrine::internal
