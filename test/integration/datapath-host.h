#ifndef PEREGRINE_TEST_INTEGRATION_DATAPATH_HOST_H_
#define PEREGRINE_TEST_INTEGRATION_DATAPATH_HOST_H_

#include <algorithm>
#include <cstddef>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

#include "absl/status/statusor.h"
#include "absl/types/span.h"
#include "src/api/transport.h"
#include "src/api/transport_types.h"

#include "test/integration/metrics.h"

namespace peregrine::integration {

// Datapath host for Peregrine integration test.
class DatapathHost final {
 public:
  DatapathHost(Component c, std::string_view control_ep,
               std::string_view data_ep, std::string_view peer_control_ep,
               std::string_view peer_data_ep, size_t buf_size, int num_conns);
  ~DatapathHost() = default;

  // Posts requests using this host's transport.
  absl::StatusOr<Handle> Post(std::string_view peer,
                              absl::Span<const Request> requests) {
    return transport_->Post(peer, requests);
  }

  // Polls the request handle.
  absl::StatusOr<Status> Poll(Handle handle) {
    return transport_->Poll(handle);
  }

  absl::Span<const Byte> Data() const { return absl::MakeConstSpan(data_); }
  Byte* DataPtr() { return data_.data(); }
  size_t DataSize() const { return data_.size(); }
  void ClearData() { std::fill(data_.begin(), data_.end(), 0); }
  void GenData();

  const std::string& endpoint() const { return data_endpoint_; }
  const std::string& peer_endpoint() const { return peer_data_endpoint_; }

 private:
  const std::string control_endpoint_;
  const std::string data_endpoint_;
  const std::string peer_control_endpoint_;
  const std::string peer_data_endpoint_;
  std::vector<Byte> data_;
  std::unique_ptr<Transport> transport_;
};

}  // namespace peregrine::integration

#endif  // PEREGRINE_TEST_INTEGRATION_DATAPATH_HOST_H_
