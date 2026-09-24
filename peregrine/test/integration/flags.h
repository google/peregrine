#ifndef PEREGRINE_TEST_INTEGRATION_FLAGS_H_
#define PEREGRINE_TEST_INTEGRATION_FLAGS_H_

#include <cstdint>
#include "absl/flags/declare.h"
#include "absl/time/time.h"

ABSL_DECLARE_FLAG(absl::Duration, test_duration);
ABSL_DECLARE_FLAG(bool, enable_ncurses);
ABSL_DECLARE_FLAG(bool, verify_data);
ABSL_DECLARE_FLAG(int64_t, buffer_size);
ABSL_DECLARE_FLAG(int, conns_per_peer);

namespace peregrine::integration {

struct Flags {
  absl::Duration test_duration;
  bool enable_ncurses = false;
  bool verify_data = false;
  int64_t buffer_size = 0;
  int conns_per_peer = 0;
};

Flags ReadFlags();

}  // namespace peregrine::integration

#endif  // PEREGRINE_TEST_INTEGRATION_FLAGS_H_
