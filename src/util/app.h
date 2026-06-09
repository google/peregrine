#ifndef PEREGRINE_SRC_UTIL_APP_H_
#define PEREGRINE_SRC_UTIL_APP_H_

#include <sys/socket.h>

#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

#include "absl/log/check.h"
#include "absl/strings/str_cat.h"
#include "absl/types/span.h"
#include "src/api/transport.h"
#include "src/api/transport_util.h"
#include "src/api/types.h"
#include "src/util/util.h"

namespace peregrine::util {

constexpr std::string_view kLocalhost = "127.0.0.1";

// This class emulates a user application that can send and receive data.
// It is thread-compatible but not thread-safe.
class App final {
 public:
  // Constructor.
  App(size_t size)
      : data_(size),
        port_(FindFreePort(AF_INET, /*tcp=*/true)),
        endpoint_(absl::StrCat(kLocalhost, ":", port_)),
        transport_(CreateTransport(endpoint_)) {
    DCHECK_GT(size, 0);
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
  const uint16_t port_;
  const std::string endpoint_;
  std::unique_ptr<Transport> transport_;
};

}  // namespace peregrine::util

#endif  // PEREGRINE_SRC_UTIL_APP_H_
