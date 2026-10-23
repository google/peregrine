#ifndef PEREGRINE_SRC_INTERNAL_CHUNK_CHUNK_TEST_UTIL_H_
#define PEREGRINE_SRC_INTERNAL_CHUNK_CHUNK_TEST_UTIL_H_

#include <cstddef>
#include <cstdint>

#include "peregrine/src/api/transport_types.h"
#include "peregrine/src/internal/assumptions.h"
#include "peregrine/src/internal/base/types.h"
#include "peregrine/src/internal/chunk/header.h"

namespace peregrine::internal::testing {

static_assert(assumptions::kBufferIsDividedIntoFixedSizeChunks);
constexpr addr_t kPayloadSrcAddr(0x12340000);  // read from
constexpr addr_t kBufferBaseAddr(0xffff0000);  // write to
static_assert(kBufferBaseAddr != kPayloadSrcAddr);
constexpr Handle kHandle(0x1234);
constexpr ReqId kReqId(0xbeef);
constexpr ReqId kReqId2(0xdead);
constexpr uint32_t kChunkSize = 1024;
constexpr uint32_t kNumChunks = 10;
constexpr chunk_t kChunkIndex(1);

// Generates chunk payload view.
ChunkPayloadView GenPayload(size_t size);

}  // namespace peregrine::internal::testing

#endif  // PEREGRINE_SRC_INTERNAL_CHUNK_CHUNK_TEST_UTIL_H_
