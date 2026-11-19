#include "peregrine/src/internal/socket/grpc_manager.h"

#include <deque>
#include <memory>
#include <string>
#include <utility>

#include "absl/base/optimization.h"
#include "absl/base/thread_annotations.h"
#include "absl/log/check.h"
#include "absl/log/log.h"
#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/strings/str_cat.h"
#include "absl/synchronization/mutex.h"
#include "absl/synchronization/notification.h"
#include "absl/time/clock.h"
#include "absl/time/time.h"
#include "grpcpp/client_context.h"
#include "grpcpp/create_channel.h"
#include "grpcpp/generic/callback_generic_service.h"
#include "grpcpp/generic/generic_stub_callback.h"
#include "grpcpp/security/credentials.h"
#include "grpcpp/security/server_credentials.h"
#include "grpcpp/server.h"
#include "grpcpp/server_builder.h"
#include "grpcpp/support/byte_buffer.h"
#include "grpcpp/support/channel_arguments.h"
#include "grpcpp/support/client_callback.h"
#include "grpcpp/support/status.h"
#include "grpcpp/support/stub_options.h"
#include "peregrine/src/internal/base/endpoint.h"

namespace peregrine::internal {

namespace {

// The gRPC method name of the call under a stream.
constexpr char kMethod[] = "/peregrine.internal.GrpcChannel/Stream";

// A stream on top of the gRPC `Reactor`, which is the client or server side of
// a bidirectional streaming call. A send is a gRPC write and a receive is a
// gRPC read, and the reaction of gRPC to the end of the operation (`On*Done()`)
// calls the callback of the operation.
//
// All the gRPC operations are started with `mu_` held, which is safe because
// gRPC never runs a reaction inside the call that starts an operation. The
// callbacks are always called without `mu_`, because they can call back into
// the stream.
template <typename Reactor>
class ReactorStream : public GrpcStream, public Reactor {
 public:
  void Send(grpc::ByteBuffer* msg, OnSendDoneFn on_done) override
      ABSL_LOCKS_EXCLUDED(mu_) {
    DCHECK(on_done != nullptr);
    {
      absl::MutexLock lock(mu_);
      DCHECK(send_done_ == nullptr) << "a send is already in progress";
      if ABSL_PREDICT_TRUE (!shutdown_ && !send_failed_ && !writing_) {
        wbuf_.Swap(msg);
        send_done_ = std::move(on_done);
        writing_ = true;
        this->StartWrite(&wbuf_);
        return;
      }
    }
    on_done(/*ok=*/false);
  }

  void Recv(OnRecvDoneFn on_done) override ABSL_LOCKS_EXCLUDED(mu_) {
    DCHECK(on_done != nullptr);
    {
      absl::MutexLock lock(mu_);
      DCHECK(recv_done_ == nullptr) << "a receive is already in progress";
      if ABSL_PREDICT_TRUE (!shutdown_ && !recv_closed_ && !reading_) {
        recv_done_ = std::move(on_done);
        reading_ = true;
        this->StartRead(&rbuf_);
        return;
      }
    }
    grpc::ByteBuffer none;
    on_done(/*ok=*/false, &none);
  }

  void Shutdown() override ABSL_LOCKS_EXCLUDED(mu_) {
    OnSendDoneFn send_done;
    OnRecvDoneFn recv_done;
    {
      absl::MutexLock lock(mu_);
      shutdown_ = true;
      // The operations in progress end now, though gRPC is not done with them
      // until it calls `OnWriteDone()` and `OnReadDone()`.
      send_done = std::exchange(send_done_, nullptr);
      recv_done = std::exchange(recv_done_, nullptr);
      maybeCloseWrites();
    }
    if (send_done != nullptr) send_done(/*ok=*/false);
    if (recv_done != nullptr) {
      grpc::ByteBuffer none;
      recv_done(/*ok=*/false, &none);
    }
  }

  // Called by gRPC when the read started by `StartRead()` is done. A false `ok`
  // means that there is no more message to read.
  void OnReadDone(bool ok) override ABSL_LOCKS_EXCLUDED(mu_) {
    OnRecvDoneFn recv_done;
    grpc::ByteBuffer msg;
    {
      absl::MutexLock lock(mu_);
      DCHECK(reading_);
      reading_ = false;
      if (ok) {
        msg.Swap(&rbuf_);
      } else {
        recv_closed_ = true;
      }
      rbuf_.Clear();
      recv_done = std::exchange(recv_done_, nullptr);
    }
    // The message is dropped if the receive has ended by a shutdown.
    if (recv_done != nullptr) recv_done(ok, &msg);
  }

  // Called by gRPC when the write started by `StartWrite()` is done. A false
  // `ok` means that the message is not sent and no more message can be sent.
  void OnWriteDone(bool ok) override ABSL_LOCKS_EXCLUDED(mu_) {
    OnSendDoneFn send_done;
    {
      absl::MutexLock lock(mu_);
      DCHECK(writing_);
      wbuf_.Clear();
      writing_ = false;
      if (!ok) send_failed_ = true;
      send_done = std::exchange(send_done_, nullptr);
      maybeCloseWrites();
    }
    if (send_done != nullptr) send_done(ok);
  }

 private:
  // Closes the write side of the gRPC call. It is called at most once, when
  // the stream is shut down and no write is in progress.
  virtual void closeWrites() = 0;

  // Closes the write side of the gRPC call if it is the time to do so.
  void maybeCloseWrites() ABSL_EXCLUSIVE_LOCKS_REQUIRED(mu_) {
    if (!shutdown_ || writing_ || writes_closed_) return;
    writes_closed_ = true;
    closeWrites();
  }

 private:
  absl::Mutex mu_;
  bool shutdown_ ABSL_GUARDED_BY(mu_) = false;
  // Whether gRPC is reading and writing, i.e., it has not called
  // `OnReadDone()` and `OnWriteDone()` yet for the operation it started.
  bool reading_ ABSL_GUARDED_BY(mu_) = false;
  bool writing_ ABSL_GUARDED_BY(mu_) = false;
  // Whether there is no more message to receive, and whether no more message
  // can be sent.
  bool recv_closed_ ABSL_GUARDED_BY(mu_) = false;
  bool send_failed_ ABSL_GUARDED_BY(mu_) = false;
  bool writes_closed_ ABSL_GUARDED_BY(mu_) = false;
  // The callbacks of the send and the receive in progress, or nullptr if there
  // is none.
  OnSendDoneFn send_done_ ABSL_GUARDED_BY(mu_);
  OnRecvDoneFn recv_done_ ABSL_GUARDED_BY(mu_);
  // The messages being received and sent, which gRPC accesses while it is
  // reading and writing, respectively.
  grpc::ByteBuffer rbuf_;
  grpc::ByteBuffer wbuf_;
};

// The client side of the gRPC call under a stream.
class ClientStream final
    : public ReactorStream<
          grpc::ClientBidiReactor<grpc::ByteBuffer, grpc::ByteBuffer>> {
 public:
  // Constructor. It starts a gRPC call to the `target` address.
  ClientStream(const std::string& target,
               std::shared_ptr<grpc::ChannelCredentials> creds,
               const grpc::ChannelArguments& args)
      : target_(target),
        stub_(grpc::CreateCustomChannel(target, std::move(creds), args)) {
    stub_.PrepareBidiStreamingCall(&context_, kMethod, grpc::StubOptions(),
                                   this);
    // The hold keeps the gRPC call alive until the destructor, because the
    // reads and writes can be started from outside of the gRPC reactions.
    AddHold();
    StartCall();
  }

  // Destructor. It fails the operations in progress, aborts the gRPC call and
  // waits for gRPC to be done with it.
  ~ClientStream() override {
    Shutdown();
    context_.TryCancel();
    RemoveHold();
    done_.WaitForNotification();
  }

  std::string ToString() const override {
    return absl::StrCat("client of ", target_);
  }

  // Called by gRPC as its last reaction.
  void OnDone(const grpc::Status& /*status*/) override { done_.Notify(); }

 private:
  void closeWrites() override { StartWritesDone(); }

 private:
  const std::string target_;
  grpc::ClientContext context_;
  grpc::GenericStubCallback stub_;
  absl::Notification done_;
};

// The server side of the gRPC call under a stream.
class ServerStream final
    : public ReactorStream<grpc::ServerGenericBidiReactor> {
 public:
  // Constructor. The `context` is owned by gRPC and is valid until `OnDone()`.
  explicit ServerStream(grpc::GenericCallbackServerContext* context)
      : client_(context->peer()), context_(context) {}

  // Destructor. It fails the operations in progress, aborts the gRPC call and
  // waits for gRPC to be done with it.
  ~ServerStream() override {
    Shutdown();
    {
      absl::MutexLock lock(context_mu_);
      if (context_ != nullptr) context_->TryCancel();
    }
    done_.WaitForNotification();
  }

  std::string ToString() const override {
    return absl::StrCat("server of ", client_);
  }

  // Called by gRPC when the call is cancelled, e.g., by the client or by the
  // shutdown of the gRPC server. The call still has to be finished.
  void OnCancel() override { Shutdown(); }

  // Called by gRPC as its last reaction.
  void OnDone() override ABSL_LOCKS_EXCLUDED(context_mu_) {
    {
      absl::MutexLock lock(context_mu_);
      context_ = nullptr;
    }
    done_.Notify();
  }

 private:
  void closeWrites() override { Finish(grpc::Status::OK); }

 private:
  const std::string client_;
  absl::Mutex context_mu_;
  grpc::GenericCallbackServerContext* context_ ABSL_GUARDED_BY(context_mu_);
  absl::Notification done_;
};

}  // namespace

// A generic gRPC service that queues the server side of the incoming streams.
class GrpcManager::Service final : public grpc::CallbackGenericService {
 public:
  // Called by gRPC for each incoming call to create the reactor of the call.
  grpc::ServerGenericBidiReactor* CreateReactor(
      grpc::GenericCallbackServerContext* context) override
      ABSL_LOCKS_EXCLUDED(mu_) {
    if ABSL_PREDICT_FALSE (context->method() != kMethod) {
      // Rejects the call as unimplemented.
      return grpc::CallbackGenericService::CreateReactor(context);
    }

    auto stream = std::make_unique<ServerStream>(context);
    grpc::ServerGenericBidiReactor* const reactor = stream.get();
    absl::MutexLock lock(mu_);
    incoming_.push_back(std::move(stream));
    return reactor;
  }

  // Waits up to `timeout` for an incoming stream.
  // Returns the stream, or nullptr if there is none within the `timeout`.
  std::unique_ptr<GrpcStream> Accept(const absl::Duration timeout)
      ABSL_LOCKS_EXCLUDED(mu_) {
    absl::MutexLock lock(mu_);
    if (!mu_.AwaitWithTimeout(absl::Condition(this, &Service::hasIncoming),
                              timeout)) {
      return nullptr;
    }
    std::unique_ptr<GrpcStream> stream = std::move(incoming_.front());
    incoming_.pop_front();
    return stream;
  }

 private:
  // Returns true iff there is an incoming stream.
  bool hasIncoming() const ABSL_SHARED_LOCKS_REQUIRED(mu_) {
    return !incoming_.empty();
  }

 private:
  absl::Mutex mu_;
  std::deque<std::unique_ptr<GrpcStream>> incoming_ ABSL_GUARDED_BY(mu_);
};

GrpcManager::GrpcManager()
    : port_(0), service_(std::make_unique<Service>()), server_(nullptr) {}

GrpcManager::~GrpcManager() { Stop(); }

absl::StatusOr<std::unique_ptr<GrpcManager>> GrpcManager::Create(
    const Endpoint& self, std::shared_ptr<grpc::ServerCredentials> creds) {
  if ABSL_PREDICT_FALSE (creds == nullptr)
    return absl::InvalidArgumentError("server credentials must not be null");

  std::unique_ptr<GrpcManager> manager(new GrpcManager());

  int port = 0;
  const std::string addr = self.ToString();
  grpc::ServerBuilder builder;
  builder.AddListeningPort(addr, std::move(creds), &port);
  builder.RegisterCallbackGenericService(manager->service_.get());
  builder.SetMaxReceiveMessageSize(
      static_cast<int>(GrpcStream::kMaxMessageSize));

  manager->server_ = builder.BuildAndStart();
  if ABSL_PREDICT_FALSE (manager->server_ == nullptr) {
    return absl::InternalError(
        absl::StrCat("GrpcManager failed to listen on ", addr));
  }

  DCHECK_GT(port, 0);
  manager->port_ = port;
  LOG(INFO) << "GrpcManager listening on port " << port;
  return manager;
}

void GrpcManager::Stop() {
  if (server_ == nullptr) return;

  // The deadline in the past makes gRPC cancel the calls of the accepted
  // streams now, rather than wait for the streams to be destroyed.
  server_->Shutdown(absl::Now());
  server_ = nullptr;
  LOG(INFO) << "GrpcManager stopped on port " << port_;
}

std::unique_ptr<GrpcStream> GrpcManager::Connect(
    const Endpoint& peer, std::shared_ptr<grpc::ChannelCredentials> creds) {
  DCHECK(peer.HasNonzeroIpPort());
  DCHECK_NE(creds, nullptr);

  grpc::ChannelArguments args;
  args.SetMaxReceiveMessageSize(static_cast<int>(GrpcStream::kMaxMessageSize));
  return std::make_unique<ClientStream>(peer.ToString(), std::move(creds),
                                        args);
}

std::unique_ptr<GrpcStream> GrpcManager::Accept(const absl::Duration timeout) {
  return service_->Accept(timeout);
}

}  // namespace peregrine::internal
