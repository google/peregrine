#ifndef PEREGRINE_TEST_BENCHMARK_WORKLOADS_WORKLOAD_GENERATOR_H_
#define PEREGRINE_TEST_BENCHMARK_WORKLOADS_WORKLOAD_GENERATOR_H_

#include "absl/status/status.h"

namespace peregrine::benchmark {

// Pure abstract interface for benchmark workload generators.
class WorkloadGenerator {
 public:
  virtual ~WorkloadGenerator() = default;

  // Runs the workload.
  virtual absl::Status Run() = 0;
};

}  // namespace peregrine::benchmark

#endif  // PEREGRINE_TEST_BENCHMARK_WORKLOADS_WORKLOAD_GENERATOR_H_
