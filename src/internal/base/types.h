#ifndef PEREGRINE_SRC_INTERNAL_BASE_TYPES_H_
#define PEREGRINE_SRC_INTERNAL_BASE_TYPES_H_

#include <sys/uio.h>

#include <cstdint>

#include "third_party/gloop/util/intops/strong_int.h"

namespace peregrine::internal {

// tcp/udp port
using port_t = uint16_t;

// hash value
using HashValue = uint64_t;

// io vector
using IoVec = ::iovec;

// `ReqId` uniquely identifies a read/write request in one process.
// `Request` is defined in `src/api/transport_types.h`.
DEFINE_STRONG_INT_TYPE(ReqId, uint32_t);

// Socket file descriptor.
DEFINE_STRONG_INT_TYPE(fd_t, int);

}  // namespace peregrine::internal

#endif  // PEREGRINE_SRC_INTERNAL_BASE_TYPES_H_
