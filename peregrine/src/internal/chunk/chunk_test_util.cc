#include "peregrine/src/internal/chunk/chunk_test_util.h"

#include <cstddef>

#include "peregrine/src/api/transport_types.h"
#include "peregrine/src/internal/chunk/header.h"

namespace peregrine::internal::testing {

ChunkPayloadView GenPayload(const size_t size) {
  auto* addr = reinterpret_cast<const Byte*>(kPayloadSrcAddr.value());
  return ChunkPayloadView(addr, size);
}

}  // namespace peregrine::internal::testing
