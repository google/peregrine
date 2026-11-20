#ifndef PEREGRINE_SRC_INTERNAL_SOCKET_GRPC_MANAGER_H_
#define PEREGRINE_SRC_INTERNAL_SOCKET_GRPC_MANAGER_H_

#include <cstddef>
#include <limits>
#include <memory>
#include <string>

#include "absl/functional/any_invocable.h"
#include "absl/status/statusor.h"
#include "absl/time/time.h"
#include "grpcpp/security/credentials.h"
#include "grpcpp/security/server_credentials.h"
#include "grpcpp/server.h"
#include "grpcpp/support/byte_buffer.h"
#include "peregrine/src/internal/base/endpoint.h"
#include "peregrine/src/util/macro.h"

namespace peregrine::internal {

// One side of a bidirectional streaming gRPC call that carries raw bytes as
// messages. It is to gRPC what a connected `TcpSocket` is to TCP: streams are
// only created by `GrpcManager`, either by connecting to a peer (the client
// side of the call) or by accepting a peer (the server side of the call), and
// both sides can send and receive.
//
// Sends and receives are asynchronous: `Send()` and `Recv()` start an
// operation and return without blocking, and the stream calls the given
// callback exactly once when the operation ends. At most one send and one
// receive can be in progress at a time, so start the next send (or receive)
// after the callback of the previous one is called, e.g., from that callback.
//
// A callback can run on a gRPC thread, or inside the `Send()`, `Recv()` or
// `Shutdown()` call that ends its operation. So callers must not hold a lock
// that their callbacks take while making these calls. A callback must not
// block and must not destroy the stream.
//
// Destroying a stream fails the operations in progress, whose callbacks are
// called before the destructor returns. It also aborts the gRPC call, so the
// messages that the peer has not received yet can be lost.
//
// `Send()`, `Recv()` and `Shutdown()` can be called concurrently with each
// other.
class GrpcStream {
 public:
  // Called when a send ends. `ok` is true iff the message is sent.
  using OnSendDoneFn = absl::AnyInvocable<void(bool ok)>;

  // Called when a receive ends. If `ok` is true, `msg` is the received
  // message, which the callback can take over by swapping it out. Otherwise
  // `msg` is empty, and there is no more message to receive: the stream is
  // shut down or the peer has closed it. `msg` is only valid during the call.
  using OnRecvDoneFn = absl::AnyInvocable<void(bool ok, grpc::ByteBuffer* msg)>;

  // The max size of a message in bytes.
  static constexpr size_t kMaxMessageSize = std::numeric_limits<int>::max();

  // Destructor.
  virtual ~GrpcStream() = default;

  // Takes over the message `msg` and starts sending it. The send fails if the
  // stream is shut down, or if this send or a previous one cannot be sent.
  // The send of a big message can stay in progress until the peer receives,
  // because of the flow control of gRPC.
  virtual void Send(grpc::ByteBuffer* msg, OnSendDoneFn on_done) = 0;

  // Starts receiving one message.
  virtual void Recv(OnRecvDoneFn on_done) = 0;

  // Shuts down the stream: fails the send and the receive in progress, and
  // all the following ones. The peer can still receive the messages sent
  // before the shutdown.
  virtual void Shutdown() = 0;

  // Returns a string representation for the stream.
  virtual std::string ToString() const = 0;
};

// This class manages the creation of gRPC streams, either passively listening
// on a local endpoint and accepting incoming streams, or actively connecting
// to a peer endpoint.
//
// Unlike `TcpManager`, it has no `Start()` because gRPC runs its own threads.
// The manager owns the gRPC server of the streams it accepted, so stopping or
// destroying it closes them. The streams made by `Connect()` do not depend on
// any manager.
//
// `Accept()` and `Connect()` are thread-safe. The other methods are
// thread-compatible.
class GrpcManager final {
 public:
  // Creates a manager listening on the `self` endpoint. Uses port 0 to listen
  // on any free port. Returns the manager on success, or an error status on
  // failure.
  static absl::StatusOr<std::unique_ptr<GrpcManager>> Create(
      const Endpoint& self, std::shared_ptr<grpc::ServerCredentials> creds);

  // Disallows copy/move operations.
  DISALLOW_COPY(GrpcManager);
  DISALLOW_MOVE(GrpcManager);

  // Destructor.
  ~GrpcManager();

  // Returns the actual TCP port bound by the manager.
  int Port() const { return port_; }

  // Stops listening and closes all the streams accepted by the manager.
  void Stop();

  // Connects to the manager listening at the `peer` endpoint, and returns the
  // client side of a new stream. The stream connects in the background, so a
  // connection failure is reported by its following sends and receives.
  static std::unique_ptr<GrpcStream> Connect(
      const Endpoint& peer, std::shared_ptr<grpc::ChannelCredentials> creds);

  // Waits up to `timeout` for a stream connected by a peer. Returns the
  // server side of the stream, or nullptr if there is none within the
  // `timeout`.
  std::unique_ptr<GrpcStream> Accept(absl::Duration timeout);

 private:
  // The gRPC service that queues the incoming streams.
  class Service;

  // Constructor.
  GrpcManager();

 private:
  int port_;
  const std::unique_ptr<Service> service_;
  std::unique_ptr<grpc::Server> server_;
};

}  // namespace peregrine::internal

#endif  // PEREGRINE_SRC_INTERNAL_SOCKET_GRPC_MANAGER_H_
