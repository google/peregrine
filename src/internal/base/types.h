#ifndef PEREGRINE_SRC_INTERNAL_BASE_TYPES_H_
#define PEREGRINE_SRC_INTERNAL_BASE_TYPES_H_

#include <sys/uio.h>

#include <cstdint>

namespace peregrine::internal {

// hash value
using HashValue = uint64_t;

// tcp/udp port
using port_t = uint16_t;

// io vector
using IoVec = ::iovec;

}  // namespace peregrine::internal

#endif  // PEREGRINE_SRC_INTERNAL_BASE_TYPES_H_
