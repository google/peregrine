#ifndef PEREGRINE_SRC_INTERNAL_BASE_TYPES_H_
#define PEREGRINE_SRC_INTERNAL_BASE_TYPES_H_

#include <sys/uio.h>

#include <cstdint>

#include "peregrine/src/util/strong_int.h"

namespace peregrine::internal {

// tcp/udp port
using port_t = uint16_t;

// io vector
using IoVec = ::iovec;

// `ReqId` uniquely identifies a read/write request in one process.
// `Request` is defined in `src/api/transport_types.h`.
DEFINE_STRONG_INT_TYPE(ReqId, uint32_t);
inline constexpr ReqId kInvalidReqId(0);

// Memory address and chunk index.
DEFINE_STRONG_INT_TYPE(addr_t, uintptr_t);
DEFINE_STRONG_INT_TYPE(chunk_t, uint32_t);

// Socket file descriptor.
DEFINE_STRONG_INT_TYPE(fd_t, int);

}  // namespace peregrine::internal

#endif  // PEREGRINE_SRC_INTERNAL_BASE_TYPES_H_
