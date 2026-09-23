#ifndef PEREGRINE_TEST_INTEGRATION_WORKLOADS_SERIAL_FIXED_WRITE_H_
#define PEREGRINE_TEST_INTEGRATION_WORKLOADS_SERIAL_FIXED_WRITE_H_

#include <cstddef>
#include <string>
#include <string_view>

#include "src/api/transport_types.h"
#include "test/integration/workloads/batch-generator.h"

namespace peregrine::integration {

// Batch generator for a single contiguous fixed-size write request per batch.
class SerialFixedWrite final : public BatchGenerator {
 public:
  SerialFixedWrite(std::string_view peer, Byte* laddr, Byte* raddr, size_t len);
  ~SerialFixedWrite() override = default;

  std::string_view Name() const override { return "serial_fixed_write"; }

  PostBatch NextBatch() const override;

 private:
  const std::string peer_;
  Byte* const laddr_;
  Byte* const raddr_;
  const size_t len_;
};

}  // namespace peregrine::integration

#endif  // PEREGRINE_TEST_INTEGRATION_WORKLOADS_SERIAL_FIXED_WRITE_H_
