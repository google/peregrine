#include "peregrine/test/workloads/serial_fixed_write.h"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <vector>

#include "absl/flags/flag.h"
#include "absl/log/check.h"
#include "peregrine/src/api/transport_types.h"

ABSL_FLAG(uint64_t, xfer_size, 1024 * 1024 * 1024ULL,
          "Buffer transfer size in bytes (default = 1 GiB)");

namespace peregrine {

SerialFixedWrite::SerialFixedWrite(uint64_t xfer_size) : xfer_size_(xfer_size) {
  QCHECK_GT(xfer_size_, 0) << "Transfer size must be greater than 0";
}

std::unique_ptr<SerialFixedWrite> SerialFixedWrite::Create() {
  const uint64_t xfer_size =
      std::max(static_cast<uint64_t>(1), absl::GetFlag(FLAGS_xfer_size));
  return std::make_unique<SerialFixedWrite>(xfer_size);
}

std::vector<peregrine::Request> SerialFixedWrite::GenerateRequests(
    peregrine::Byte* base_laddr, peregrine::Byte* base_raddr) const {
  CHECK(base_laddr != nullptr);
  CHECK(base_raddr != nullptr);
  return {
      peregrine::Request{
          .op = peregrine::Op::kWrite,
          .laddr = base_laddr,
          .raddr = base_raddr,
          .len = static_cast<size_t>(xfer_size_),
      },
  };
}

}  // namespace peregrine
