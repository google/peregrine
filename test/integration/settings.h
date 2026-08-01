#ifndef PEREGRINE_TEST_INTEGRATION_SETTINGS_H_
#define PEREGRINE_TEST_INTEGRATION_SETTINGS_H_

#include "absl/time/time.h"

namespace peregrine::integration {

struct Settings {
  absl::Time test_begin;
  absl::Duration test_duration;
};

}  // namespace peregrine::integration

#endif  // PEREGRINE_TEST_INTEGRATION_SETTINGS_H_
