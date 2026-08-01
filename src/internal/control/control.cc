#include "src/internal/control/control.h"

#include <netinet/in.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <string_view>
#include <utility>

#include "absl/base/optimization.h"
#include "absl/log/check.h"
#include "absl/log/log.h"
#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/synchronization/mutex.h"
#include "grpcpp/security/credentials.h"
#include "grpcpp/security/server_credentials.h"
#include "src/api/transport_types.h"
#include "src/internal/assumptions.h"
#include "src/internal/base/types.h"
#include "src/internal/channel/channel.h"
#include "src/internal/channel/channel_types.h"
#include "src/internal/control/grpc_client.h"
#include "src/internal/control/grpc_server.h"
#include "src/internal/control/message.h"
#include "src/internal/control/message.pb.h"

namespace peregrine::internal {

namespace {
constexpr std::string_view kControl = "control plane ";
}  // namespace

Control::Control(std::unique_ptr<Channel> channel)
    : stopping_(false),
      channel_(std::move(channel)),
      send_thread_([this]() { sendLoop(); }),
      recv_thread_([this]() { recvLoop(); }),
      grpc_server_(nullptr),
      client_creds_(nullptr) {
  DCHECK(IsReliableStream(channel_->Type()));
  LOG(INFO) << kControl << "created in legacy TCP mode";
}

Control::Control(std::unique_ptr<GrpcServer> server,
                 std::shared_ptr<grpc::ChannelCredentials> client_creds)
    : stopping_(false),
      channel_(nullptr),
      grpc_server_(std::move(server)),
      client_creds_(std::move(client_creds)) {
  LOG(INFO) << kControl << "created in gRPC mode";
}

Control::~Control() {
  {
    absl::MutexLock _(mu_);
    if (channel_ != nullptr) {
      channel_->Shutdown();
    }
    if (grpc_server_ != nullptr) {
      grpc_server_->Shutdown();
    }
    stopping_ = true;
  }
  // all threads are joined in their destructors.
  LOG(INFO) << kControl << "destroyed";
}

absl::StatusOr<std::unique_ptr<Control>> Control::CreateGrpcControl(
    std::string_view listen_addr, RequestHandler&& handler,
    std::shared_ptr<grpc::ServerCredentials> server_creds,
    std::shared_ptr<grpc::ChannelCredentials> client_creds) {
  if (server_creds == nullptr || client_creds == nullptr) {
    return absl::InvalidArgumentError(
        "Control::CreateGrpcControl requires explicit server and client "
        "credentials");
  }
  auto server_or = GrpcServer::Create(listen_addr, std::move(handler),
                                      std::move(server_creds));
  if (!server_or.ok()) {
    return server_or.status();
  }
  return std::unique_ptr<Control>(
      new Control(std::move(*server_or), std::move(client_creds)));
}

absl::StatusOr<proto::RespMsg> Control::SendRequest(std::string_view peer_addr,
                                                    const proto::ReqMsg& req) {
  if (client_creds_ == nullptr) {
    return absl::FailedPreconditionError(
        "Control::SendRequest invoked without configured gRPC credentials");
  }
  GrpcClient* client = nullptr;
  {
    absl::MutexLock _(mu_);
    client = getOrCreateClient(peer_addr);
  }
  return client->SendUnary(req);
}

GrpcClient* Control::getOrCreateClient(std::string_view peer_addr) {
  auto it = peer_clients_.find(peer_addr);
  if (it == peer_clients_.end()) {
    auto [inserted_it, _] = peer_clients_.emplace(
        peer_addr, std::make_unique<GrpcClient>(peer_addr, client_creds_));
    return inserted_it->second.get();
  }
  return it->second.get();
}

int Control::port() const {
  return grpc_server_ != nullptr ? grpc_server_->port() : 0;
}

bool Control::EnqueueSend(const proto::ReqMsg& msg) {
  absl::MutexLock _(mu_);
  send_queue_.push_back(msg);
  return true;
}

bool Control::DequeueRecv(proto::ReqMsg& msg) {
  absl::MutexLock _(mu_);
  if (recv_queue_.empty()) {
    return false;
  }
  msg = std::move(recv_queue_.front());
  recv_queue_.pop_front();
  return true;
}

bool Control::hasSendWork() const { return !send_queue_.empty() || stopping_; }

void Control::sendLoop() {
  proto::ReqMsg msg;
  while (true) {
    {  // step 1: get a message.
      absl::MutexLock _(mu_);
      mu_.Await(absl::Condition(this, &Control::hasSendWork));
      if (send_queue_.empty()) {
        DCHECK(stopping_);  // destructor called
        break;
      }
      msg = std::move(send_queue_.front());
      send_queue_.pop_front();
    }

    // step 2: send the message.
    send(msg);
  }
  LOG(INFO) << kControl << "send loop exited";
}

void Control::recvLoop() {
  proto::ReqMsg msg;
  while (recv(msg)) {
    absl::MutexLock _(mu_);
    recv_queue_.push_back(msg);
  }
  LOG(INFO) << kControl << "recv loop exited";
}

bool Control::send(const proto::ReqMsg& msg) {
  static_assert(assumptions::kThereIsOnlyOneWrapperControlMessageAtMost1KiB);
  DCHECK(IsReliableStream(channel_->Type()));

  // Serialize the control message: 4-byte length + payload.
  const std::string s = Message::Serialize(msg);
  const size_t size = s.size();
  if ABSL_PREDICT_FALSE (size > Message::kMaxLen) {
    LOG(WARNING) << "control msg too large: " << size;
    return false;
  }
  const uint32_t len = static_cast<uint32_t>(size);
  const uint32_t len_nbo = htonl(len);

  // Send the serialized control message.
  const std::array<const IoVec, 2> iovecs = {
      IoVec((void*)&len_nbo, sizeof(len_nbo)),
      IoVec((void*)s.data(), size),
  };
  return channel_->Write(iovecs) == sizeof(len_nbo) + size;
}

bool Control::recv(proto::ReqMsg& msg) {
  static_assert(assumptions::kThereIsOnlyOneWrapperControlMessageAtMost1KiB);
  DCHECK(IsReliableStream(channel_->Type()));

  // Clear the message before receiving a new one.
  msg.Clear();

  // Read the 4-byte network-byte-order length.
  uint32_t len_nbo;
  Byte* len_buf = reinterpret_cast<Byte*>(&len_nbo);
  const ssize_t len = channel_->Read(len_buf, sizeof(len_nbo));
  if ABSL_PREDICT_FALSE (std::cmp_not_equal(len, sizeof(len_nbo))) {
    LOG(WARNING) << "failed to read control msg len: " << len;
    return false;
  }

  // Read the control message payload.
  Byte buf[Message::kMaxLen];
  const uint32_t size = ntohl(len_nbo);
  if ABSL_PREDICT_FALSE (size > Message::kMaxLen) {
    LOG(WARNING) << "control msg too large: " << size;
    return false;
  }
  const ssize_t len2 = channel_->Read(buf, size);
  if ABSL_PREDICT_FALSE (std::cmp_not_equal(len2, size)) {
    LOG(WARNING) << "failed to read control msg payload: " << len2;
    return false;
  }

  // Deserialize the control message.
  const std::string_view s(reinterpret_cast<const char*>(buf), len2);
  return Message::Deserialize(s, msg);
}

}  // namespace peregrine::internal
