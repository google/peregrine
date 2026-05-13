#ifndef PEREGRINE_TEST_INTEGRATION_TEST_UTIL_H_
#define PEREGRINE_TEST_INTEGRATION_TEST_UTIL_H_

#include <cstddef>
#include <memory>
#include <vector>

#include "absl/log/check.h"
#include "absl/types/span.h"
#include "src/api/transport.h"
#include "src/api/transport_util.h"
#include "src/api/types.h"

namespace peregrine::integration_test {

// This class emulates a user process that can send and receive data.
// It is thread-compatible but not thread-safe.
class UserProcess final {
 public:
  // Constructor.
  UserProcess(const size_t len, const Byte byte)
      : data_(len, byte), transport_(CreateTransport()) {
    CHECK_NE(transport_, nullptr);  // Crash OK
  }

  // Returns the data buffer.
  absl::Span<Byte> Data() { return absl::MakeSpan(data_); }

  // Returns the transport.
  Transport& GetTransport() { return *transport_; }

 private:
  std::vector<Byte> data_;
  std::unique_ptr<Transport> transport_;
};

}  // namespace peregrine::integration_test

#endif  // PEREGRINE_TEST_INTEGRATION_TEST_UTIL_H_
