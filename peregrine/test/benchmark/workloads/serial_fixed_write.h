#ifndef PEREGRINE_TEST_BENCHMARK_WORKLOADS_SERIAL_FIXED_WRITE_H_
#define PEREGRINE_TEST_BENCHMARK_WORKLOADS_SERIAL_FIXED_WRITE_H_

#include <cstdint>
#include <memory>
#include <string_view>
#include <vector>

#include "peregrine/src/api/transport_types.h"
#include "peregrine/test/benchmark/workloads/workload_generator.h"

namespace peregrine::benchmark {

// Workload generator for a single contiguous fixed-size write request.
class SerialFixedWrite : public WorkloadGenerator {
 public:
  explicit SerialFixedWrite(uint64_t xfer_size);

  // Creates a SerialFixedWrite generator from CLI flags.
  static std::unique_ptr<SerialFixedWrite> Create();

  std::string_view Name() const override { return "serial_fixed_write"; }

  uint64_t TotalSizeBytes() const override { return xfer_size_; }

  std::vector<peregrine::Request> GenerateRequests(
      peregrine::Byte* base_laddr, peregrine::Byte* base_raddr) const override;

 private:
  const uint64_t xfer_size_;
};

}  // namespace peregrine::benchmark

#endif  // PEREGRINE_TEST_BENCHMARK_WORKLOADS_SERIAL_FIXED_WRITE_H_
