#ifndef PEREGRINE_TEST_INTEGRATION_DATAPATH_HOST_H_
#define PEREGRINE_TEST_INTEGRATION_DATAPATH_HOST_H_

#include <algorithm>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

#include "absl/status/statusor.h"
#include "absl/strings/string_view.h"
#include "absl/types/span.h"
#include "src/api/transport.h"
#include "src/api/transport_types.h"

namespace peregrine::integration {

// Datapath host for Peregrine integration test.
class DatapathHost final {
 public:
  DatapathHost(absl::string_view name, absl::string_view control_ep,
               absl::string_view data_ep, absl::string_view peer_control_ep,
               absl::string_view peer_data_ep, size_t buf_size, int num_conns);
  ~DatapathHost() = default;

  std::string DebugString() const;

  void IncrementTransfers(int64_t bytes) {
    n_transfers_++;
    n_bytes_ += bytes;
  }

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

  int64_t n_transfers() const { return n_transfers_; }
  int64_t n_bytes() const { return n_bytes_; }
  const std::string& endpoint() const { return data_endpoint_; }
  const std::string& peer_endpoint() const { return peer_data_endpoint_; }

 private:
  const std::string name_;
  const std::string control_endpoint_;
  const std::string data_endpoint_;
  const std::string peer_control_endpoint_;
  const std::string peer_data_endpoint_;
  const std::string status_;
  std::vector<Byte> data_;
  std::unique_ptr<Transport> transport_;
  std::atomic<int64_t> n_transfers_ = 0;
  std::atomic<int64_t> n_bytes_ = 0;
};

}  // namespace peregrine::integration

#endif  // PEREGRINE_TEST_INTEGRATION_DATAPATH_HOST_H_
