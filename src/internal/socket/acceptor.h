#ifndef PEREGRINE_SRC_INTERNAL_SOCKET_ACCEPTOR_H_
#define PEREGRINE_SRC_INTERNAL_SOCKET_ACCEPTOR_H_

#include <atomic>
#include <memory>
#include <utility>

#include "absl/functional/any_invocable.h"
#include "absl/log/check.h"
#include "src/internal/base/endpoint.h"
#include "src/internal/socket/socket_tcp.h"

namespace peregrine::internal {

// This class listens on a local endpoint, accepts incoming connections, and
// creates a new tcp socket for each connection.
// It is thread-compatible but not thread-safe.
class TcpAcceptor {
  using AcceptCallback = absl::AnyInvocable<void(std::unique_ptr<TcpSocket>)>;

 public:
  // Creates a tcp acceptor with a socket listening on the `local` endpoint.
  static std::unique_ptr<TcpAcceptor> Create(const Endpoint& local);

  // Returns the underlying tcp listen socket.
  const TcpSocket& Socket() const { return *listener_; }

  // Starts running the acceptor.
  void Start(AcceptCallback accept);

  // Stops the acceptor.
  void Stop();

 private:
  // Constructor with a valid tcp listen socket.
  explicit TcpAcceptor(std::unique_ptr<TcpSocket> socket)
      : stop_(false), listener_(std::move(socket)) {
    DCHECK(invariant());
  }

  // Returns true iff the acceptor is in a valid state.
  bool invariant() const {
    return listener_ != nullptr && listener_->IsValid();
  }

 private:
  std::atomic<bool> stop_;
  std::unique_ptr<TcpSocket> listener_;
};

}  // namespace peregrine::internal

#endif  // PEREGRINE_SRC_INTERNAL_SOCKET_ACCEPTOR_H_
