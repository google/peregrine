#ifndef PEREGRINE_SRC_INTERNAL_BASE_TYPES_H_
#define PEREGRINE_SRC_INTERNAL_BASE_TYPES_H_

#include <cstdint>

#include "absl/strings/string_view.h"

namespace peregrine {

// IP address.
using ipaddr_t = absl::string_view;

// TCP/UDP port.
using port_t = uint16_t;

}  // namespace peregrine

#endif  // PEREGRINE_SRC_INTERNAL_BASE_TYPES_H_
