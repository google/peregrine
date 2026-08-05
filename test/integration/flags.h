#ifndef PEREGRINE_TEST_INTEGRATION_FLAGS_H_
#define PEREGRINE_TEST_INTEGRATION_FLAGS_H_

#include "absl/flags/declare.h"
#include "absl/time/time.h"

ABSL_DECLARE_FLAG(absl::Duration, test_duration);
ABSL_DECLARE_FLAG(bool, enable_ncurses);

#endif  // PEREGRINE_TEST_INTEGRATION_FLAGS_H_
