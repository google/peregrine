#include <algorithm>
#include <cstdint>

#include "absl/flags/flag.h"
#include "absl/log/check.h"
#include "absl/time/time.h"
#include "test/integration/flags.h"

ABSL_FLAG(absl::Duration, test_duration, absl::Seconds(30),
          "Integration test duration.");

ABSL_FLAG(bool, enable_ncurses, true, "Set to false to disable ncurses.");

ABSL_FLAG(bool, verify_data, true,
          "Verify data integrity after each transfer.");

ABSL_FLAG(int64_t, buffer_size, 1024 * 1024, "Buffer size in bytes.");

ABSL_FLAG(int, conns_per_peer, 1, "Number of connections per peer.");

namespace peregrine::integration {

Flags ReadFlags() {
  Flags flags;
  flags.buffer_size = absl::GetFlag(FLAGS_buffer_size);
  CHECK_GT(flags.buffer_size, 0) << "buffer_size must be positive";

  flags.conns_per_peer = absl::GetFlag(FLAGS_conns_per_peer);
  CHECK_GT(flags.conns_per_peer, 0) << "conns_per_peer must be positive";

  flags.verify_data = absl::GetFlag(FLAGS_verify_data);
  flags.test_duration =
      std::max(absl::Seconds(1), absl::GetFlag(FLAGS_test_duration));
  flags.enable_ncurses = absl::GetFlag(FLAGS_enable_ncurses);

  return flags;
}

}  // namespace peregrine::integration
