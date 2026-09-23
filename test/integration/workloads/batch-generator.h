#ifndef PEREGRINE_TEST_INTEGRATION_WORKLOADS_BATCH_GENERATOR_H_
#define PEREGRINE_TEST_INTEGRATION_WORKLOADS_BATCH_GENERATOR_H_

#include <string_view>
#include <vector>

#include "absl/functional/any_invocable.h"
#include "absl/time/time.h"
#include "src/api/transport_types.h"

namespace peregrine::integration {

struct PostItem {
  std::string_view peer;
  std::vector<Request> requests;
  absl::AnyInvocable<void(Status)> on_complete = nullptr;
};

struct PostBatch {
  std::vector<PostItem> items;
  absl::Duration sleep = absl::ZeroDuration();
};

// Abstract interface for workload batch generators in integration tests.
class BatchGenerator {
 public:
  virtual ~BatchGenerator() = default;

  // Name of the workload (e.g. "serial_fixed_write").
  virtual std::string_view Name() const = 0;

  // Generates the next PostBatch.
  virtual PostBatch NextBatch() const = 0;
};

}  // namespace peregrine::integration

#endif  // PEREGRINE_TEST_INTEGRATION_WORKLOADS_BATCH_GENERATOR_H_
