#ifndef PEREGRINE_SRC_API_V1_CONN_POOL_H_
#define PEREGRINE_SRC_API_V1_CONN_POOL_H_

#include <cstddef>
#include <memory>
#include <string>
#include <vector>

#include "absl/base/thread_annotations.h"
#include "absl/container/flat_hash_map.h"
#include "absl/log/check.h"
#include "absl/status/statusor.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/string_view.h"
#include "absl/synchronization/mutex.h"
#include "grpcpp/channel.h"

namespace peregrine::v1 {

// This class manages a pool of TCP connections between multiple pairs of
// local and remote peers.
// It is thread-safe.
class ConnPool final {
 public:
  // Constructor.
  ConnPool() : stop_(false) {}

  // Destructor.
  ~ConnPool() { DCHECK(stop_ && pool_.empty()); }

  // Closes all connections.
  void Close();

  // Borrows a connection from the pool. If no connection is available, creates
  // a new one. Returns the socket descriptor of the connection if successful.
  // Otherwise returns an error status.
  absl::StatusOr<int> Borrow(absl::string_view peer,
                             absl::string_view local_ip = "",
                             bool require_psp = false,
                             std::shared_ptr<grpc::Channel> channel = nullptr);

  // Returns a connection to the pool if ok is true. Otherwise, closes the
  // connection.
  void Return(bool ok, int fd, absl::string_view peer,
              absl::string_view local_ip = "");

  // Closes every idle pooled connection to `peer` under any local IP and
  // returns how many were closed. A connection that is currently borrowed is
  // not in the pool and is unaffected. Callers release a peer's idle
  // connections once their transfers to it have settled, so a later transfer
  // dials afresh instead of the process holding one socket per
  // (local IP, peer, stream) for its whole life.
  size_t CloseIdle(absl::string_view peer);

  // Number of idle pooled connections to `peer` under any local IP.
  size_t IdleCount(absl::string_view peer) const;

 private:
  using Key = std::string;
  using Fds = std::vector<int>;

  // Generates a pool key from the local/peer ip address pair.
  static Key GenPoolKey(absl::string_view peer, absl::string_view local_ip) {
    return absl::StrCat(local_ip, "->", peer);
  }

  // Generates the pool key suffix matching any local IP for `peer`.
  static Key GenPoolKeySuffix(absl::string_view peer) {
    return absl::StrCat("->", peer);
  }

 private:
  mutable absl::Mutex mu_;
  bool stop_ ABSL_GUARDED_BY(mu_);
  absl::flat_hash_map<Key, Fds> pool_ ABSL_GUARDED_BY(mu_);
};

}  // namespace peregrine::v1

#endif  // PEREGRINE_SRC_API_V1_CONN_POOL_H_
