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

// This class emulates a user application that can send and receive data.
// It is thread-compatible but not thread-safe.
class UserApplication final {
 public:
  // Constructor.
  UserApplication(const size_t len)
      : data_(len), transport_(CreateTransport()) {
    CHECK_NE(transport_, nullptr);  // Crash OK
  }

  // Returns the data pointer.
  Byte* DataPtr() { return data_.data(); }

  // Returns the data size.
  size_t DataSize() const { return data_.size(); }

  // Returns the data buffer.
  absl::Span<const Byte> Data() const { return absl::MakeConstSpan(data_); }

  // Returns the transport.
  Transport& GetTransport() { return *transport_; }

  // Sets all the data to zero.
  void ClearData();

  // Sets all the data to non-zero random values.
  void GenData();

 private:
  std::vector<Byte> data_;
  std::unique_ptr<Transport> transport_;
};

}  // namespace peregrine::integration_test

#endif  // PEREGRINE_TEST_INTEGRATION_TEST_UTIL_H_
