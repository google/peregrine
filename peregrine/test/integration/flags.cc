#include "peregrine/test/integration/flags.h"

#include <algorithm>
#include <string>

#include "absl/flags/flag.h"
#include "absl/log/check.h"
#include "absl/time/time.h"
#include "peregrine/test/integration/error_inject/error_inject_util.h"
#include "peregrine/test/workloads/workload_generator.h"

ABSL_FLAG(absl::Duration, test_duration, absl::Seconds(30),
          "Integration test duration.");

ABSL_FLAG(bool, enable_ncurses, true, "Set to false to disable ncurses.");

ABSL_FLAG(bool, verify_data, true,
          "Verify data integrity after each transfer.");

ABSL_FLAG(int, conns_per_peer, 1, "Number of connections per peer.");

ABSL_FLAG(std::string, workload, "serial_fixed_write",
          "Workload type to run: 'serial_fixed_write', 'kv_cache'");

ABSL_FLAG(std::string, error_inject, "none",
          "Error injector to enable: 'none', 'mid_chunk_disconnect'");

namespace peregrine::integration {

Flags ReadFlags() {
  Flags flags;
  flags.conns_per_peer = absl::GetFlag(FLAGS_conns_per_peer);
  CHECK_GT(flags.conns_per_peer, 0) << "conns_per_peer must be positive";

  flags.verify_data = absl::GetFlag(FLAGS_verify_data);
  flags.test_duration =
      std::max(absl::Seconds(1), absl::GetFlag(FLAGS_test_duration));
  flags.enable_ncurses = absl::GetFlag(FLAGS_enable_ncurses);
  flags.workload = workloads::ParseWorkloadType(absl::GetFlag(FLAGS_workload));
  flags.error_inject =
      ParseErrorInjectorType(absl::GetFlag(FLAGS_error_inject));

  return flags;
}

}  // namespace peregrine::integration
