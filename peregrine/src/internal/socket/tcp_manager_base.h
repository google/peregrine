#ifndef PEREGRINE_SRC_INTERNAL_SOCKET_TCP_MANAGER_BASE_H_
#define PEREGRINE_SRC_INTERNAL_SOCKET_TCP_MANAGER_BASE_H_

#include <memory>
#include <utility>
#include <vector>

#include "absl/base/nullability.h"
#include "absl/base/thread_annotations.h"
#include "absl/container/flat_hash_map.h"
#include "absl/container/node_hash_map.h"
#include "absl/log/check.h"
#include "absl/synchronization/mutex.h"
#include "peregrine/src/internal/base/types.h"
#include "peregrine/src/internal/socket/socket_tcp.h"

namespace peregrine::internal {

// This class exists to make `TcpManager` code simpler. It should not have
// any data members, but only defines data structures used by `TcpManager`.
class TcpManagerBase {
 protected:
  // A listening socket.
  struct Listener {
    std::unique_ptr<TcpSocket> socket;
  };

  // A connecting socket.
  struct Connector {
    std::unique_ptr<TcpSocket> socket;
  };

  // A map of non-blocking tcp listening sockets, each keyed by its file
  // descriptor. This class is thread-safe.
  class Listeners {
   public:
    // Constructor with a set of non-blocking tcp listening sockets.
    explicit Listeners(absl::node_hash_map<fd_t, Listener> sockets)
        : fd2skts_(std::move(sockets)) {
      DCHECK(Invariant());
    }

    // Returns true iff all the sockets are valid and non-blocking.
    bool Invariant() const;

    // Returns a pointer (which is stable) to the listening socket of the
    // file descriptor `fd`. If it does not exist, returns nullptr.
    const TcpSocket* Get(fd_t fd) const ABSL_LOCKS_EXCLUDED(mu_);

    // Removes the listening socket of the file descriptor `fd`.
    void Remove(fd_t fd) ABSL_LOCKS_EXCLUDED(mu_);

    // Shuts down all the listening sockets.
    void Shutdown() ABSL_LOCKS_EXCLUDED(mu_);

    // Removes and closes all the listening sockets.
    void Clear() ABSL_LOCKS_EXCLUDED(mu_);

   private:
    mutable absl::Mutex mu_;
    // Pointer stability is required.
    absl::node_hash_map<fd_t, Listener> fd2skts_ ABSL_GUARDED_BY(mu_);
  };

  // A map of non-blocking tcp connecting sockets, each keyed by its file
  // descriptor. This class is thread-safe.
  class Connectors {
   public:
    // Returns true iff all the sockets are valid.
    bool Invariant() const ABSL_LOCKS_EXCLUDED(mu_);

    // Adds a connecting socket.
    void Add(fd_t fd, absl_nonnull std::unique_ptr<TcpSocket> socket)
        ABSL_LOCKS_EXCLUDED(mu_);

    // Returns the connecting socket of the file descriptor `fd` and removes it
    // from the internal container. If `fd` does not exist, returns nullptr.
    std::unique_ptr<TcpSocket> Remove(fd_t fd) ABSL_LOCKS_EXCLUDED(mu_);

    // Removes and closes all the connecting sockets.
    void Clear() ABSL_LOCKS_EXCLUDED(mu_);

   private:
    mutable absl::Mutex mu_;
    absl::flat_hash_map<fd_t, Connector> fd2skts_ ABSL_GUARDED_BY(mu_);
  };

  // A number of produced sockets that are in the connected state.
  // This class is thread-safe.
  class Produced {
   public:
    // Returns true iff all the sockets are non-null and connected.
    bool Invariant() const ABSL_LOCKS_EXCLUDED(mu_);

    // Adds a connected socket.
    void Add(absl_nonnull std::unique_ptr<TcpSocket> socket)
        ABSL_LOCKS_EXCLUDED(mu_);

    // Returns all the connected sockets and clears the internal container.
    // After this call, `Add()` might be called and refill the container.
    std::vector<std::unique_ptr<TcpSocket>> MoveAll() ABSL_LOCKS_EXCLUDED(mu_);

    // Removes and closes all the connected sockets.
    void Clear() ABSL_LOCKS_EXCLUDED(mu_);

   private:
    mutable absl::Mutex mu_;
    std::vector<std::unique_ptr<TcpSocket>> sockets_ ABSL_GUARDED_BY(mu_);
  };
};

}  // namespace peregrine::internal

#endif  // PEREGRINE_SRC_INTERNAL_SOCKET_TCP_MANAGER_BASE_H_
