#ifndef PEREGRINE_SRC_UTIL_APP_H_
#define PEREGRINE_SRC_UTIL_APP_H_

#include <sys/socket.h>

#include <algorithm>
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
#include "src/api/transport_types.h"
#include "src/api/transport_util.h"
#include "src/util/util.h"

namespace peregrine::util {

constexpr std::string_view kLocalhost = "127.0.0.1";

// This class emulates a user application that can send and receive data.
// It is thread-compatible but not thread-safe.
class App final {
 public:
  // Constructor.
  App(size_t size, int num_conns_per_peer)
      : data_(size),
        control_plane_listener_(createEndpoint(AF_INET)),
        data_plane_listener_(createEndpoint(AF_INET)),
        host_(absl::StrCat(control_plane_listener_, ",", data_plane_listener_)),
        transport_(CreateTransport(host_, num_conns_per_peer)) {
    DCHECK_GT(size, 0);
    CHECK_NE(transport_, nullptr);  // Crash OK
  }

  // Returns the endpoint.
  std::string GetEndpoint() const { return control_plane_listener_; }

  // Returns the transport.
  Transport& GetTransport() const { return *transport_; }

  // Returns the data pointer.
  Byte* DataPtr() { return data_.data(); }

  // Returns the data size.
  size_t DataSize() const { return data_.size(); }

  // Returns the data buffer.
  absl::Span<const Byte> Data() const { return absl::MakeConstSpan(data_); }

  // Sets all the data to zero.
  void ClearData() { std::fill(data_.begin(), data_.end(), 0); }

  // Sets all the data to nonzero random values.
  void GenData() { RandomNonZero(absl::MakeSpan(data_)); }

 private:
  // Creates an endpoint in the given address family.
  static std::string createEndpoint(int family) {
    const uint16_t port = FindFreePort(family, /*tcp=*/true);
    return absl::StrCat(kLocalhost, ":", port);
  }

 private:
  std::vector<Byte> data_;
  const std::string control_plane_listener_;
  const std::string data_plane_listener_;
  const std::string host_;
  std::unique_ptr<Transport> transport_;
};

}  // namespace peregrine::util

#endif  // PEREGRINE_SRC_UTIL_APP_H_
