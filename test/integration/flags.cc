#include "test/integration/flags.h"

#include "absl/flags/flag.h"
#include "absl/time/time.h"

ABSL_FLAG(absl::Duration, test_duration, absl::Seconds(30),
          "Integration test duration.");
