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

// `Buffer` uniquely identifies a buffer (a contiguous memory space).
DEFINE_STRONG_INT_TYPE(Buffer, uint32_t);

}  // namespace peregrine::internal

#endif  // PEREGRINE_SRC_INTERNAL_BASE_TYPES_H_
