#ifndef PEREGRINE_TEST_BENCHMARK_WORKLOADS_SERIAL_FIXED_WRITE_H_
#define PEREGRINE_TEST_BENCHMARK_WORKLOADS_SERIAL_FIXED_WRITE_H_

#include <cstdint>
#include <memory>
#include <string>
#include <string_view>

#include "absl/status/status.h"
#include "src/api/transport.h"
#include "test/benchmark/workloads/workload_generator.h"

namespace peregrine::benchmark {

class SerialFixedWrite : public WorkloadGenerator {
 public:
  SerialFixedWrite(Transport* transport, int app_control_fd,
                   std::string_view server_endpoint, uint64_t xfer_size,
                   uint32_t num_xfers);

  // Creates a SerialFixedWrite after validating --xfer_size and --num_xfers.
  static std::unique_ptr<SerialFixedWrite> Create(
      Transport* transport, int app_control_fd,
      std::string_view server_endpoint);

  absl::Status Run() override;

 private:
  Transport* const transport_;
  const int app_control_fd_;
  const std::string server_endpoint_;
  const uint64_t xfer_size_;
  const uint32_t num_xfers_;
};

}  // namespace peregrine::benchmark

#endif  // PEREGRINE_TEST_BENCHMARK_WORKLOADS_SERIAL_FIXED_WRITE_H_
