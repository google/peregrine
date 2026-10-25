#ifndef PEREGRINE_SRC_API_V1_CONN_UTIL_H_
#define PEREGRINE_SRC_API_V1_CONN_UTIL_H_

#include <cstdint>
#include <string_view>

#include "absl/status/statusor.h"

namespace peregrine::v1 {

struct ConnectTiming {
  int64_t connect_us = -1;
  int64_t psp_key_xchg_us = -1;
};

// Connects to remote TCP peer with optional local IP binding.
// The connect times out after a fixed bound.
absl::StatusOr<int> ConnectToPeer(std::string_view peer,
                                  std::string_view local_ip = "",
                                  ConnectTiming* timing = nullptr);

}  // namespace peregrine::v1

#endif  // PEREGRINE_SRC_API_V1_CONN_UTIL_H_
