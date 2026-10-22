#ifndef PEREGRINE_SRC_API_V1_CONN_UTIL_H_
#define PEREGRINE_SRC_API_V1_CONN_UTIL_H_

#include <string_view>

#include "absl/status/statusor.h"

namespace peregrine::v1 {

struct ConnectTiming {
  double connect_ms = 0.0;
  double psp_key_exchange_ms = 0.0;
};

// Connects to remote TCP peer with optional local IP binding.
// The connect times out after a fixed bound.
absl::StatusOr<int> ConnectToPeer(std::string_view peer,
                                  std::string_view local_ip = "",
                                  ConnectTiming* timing = nullptr);

}  // namespace peregrine::v1

#endif  // PEREGRINE_SRC_API_V1_CONN_UTIL_H_
