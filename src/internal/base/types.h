#ifndef PEREGRINE_SRC_INTERNAL_BASE_TYPES_H_
#define PEREGRINE_SRC_INTERNAL_BASE_TYPES_H_

#include <sys/types.h>
#include <sys/uio.h>

#include <cstdint>

#include "absl/strings/string_view.h"

namespace peregrine {

// hash value
using HashValue = uint64_t;

// ip address
using ipaddr_t = absl::string_view;

// tcp/udp port
using port_t = uint16_t;

// io vector
using iovec_t = struct iovec;

}  // namespace peregrine

#endif  // PEREGRINE_SRC_INTERNAL_BASE_TYPES_H_
