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
#include "absl/synchronization/mutex.h"
#include "src/api/types.h"
#include "src/internal/assumptions.h"
#include "src/internal/base/types.h"
#include "src/internal/channel/channel.h"
#include "src/internal/control/message.pb.h"
#include "src/internal/control/parser.h"

namespace peregrine::internal {

namespace {
constexpr std::string_view kControl = "control plane ";
}  // namespace

Control::Control(std::unique_ptr<Channel> channel)
    : stopping_(false),
      channel_(std::move(channel)),
      send_thread_([this]() { sendLoop(); }),
      recv_thread_([this]() { recvLoop(); }) {
  DCHECK(IsReliableStream(channel_->Type()));
  LOG(INFO) << kControl << "created";
}

Control::~Control() {
  {
    absl::MutexLock _(mu_);
    channel_->Shutdown();
    stopping_ = true;
  }
  // all threads are joined in their destructors.
  LOG(INFO) << kControl << "destroyed";
}

bool Control::EnqueueSend(const proto::Control& c) {
  absl::MutexLock _(mu_);
  send_queue_.push_back(c);
  return true;
}

bool Control::DequeueRecv(proto::Control& c) {
  absl::MutexLock _(mu_);
  if (recv_queue_.empty()) {
    return false;
  }
  c = std::move(recv_queue_.front());
  recv_queue_.pop_front();
  return true;
}

bool Control::hasSendWork() const { return !send_queue_.empty() || stopping_; }

void Control::sendLoop() {
  proto::Control msg;
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
  proto::Control msg;
  while (recv(msg)) {
    DCHECK(msg.request().op() != proto::Request::INVALID);

    absl::MutexLock _(mu_);
    recv_queue_.push_back(msg);
  }
  LOG(INFO) << kControl << "recv loop exited";
}

bool Control::send(const proto::Control& msg) {
  static_assert(assumptions::kThereIsOnlyOneWrapperControlMessage);
  DCHECK(IsReliableStream(channel_->Type()));

  // Serialize the control message: 4-byte length + payload.
  const std::string s = ControlMsg::Serialize(msg);
  const size_t size = s.size();
  if ABSL_PREDICT_FALSE (size > ControlMsg::kMaxLen) {
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
  return channel_->Write(iovecs);
}

bool Control::recv(proto::Control& msg) {
  static_assert(assumptions::kThereIsOnlyOneWrapperControlMessage);
  DCHECK(IsReliableStream(channel_->Type()));

  // Clear the message before receiving a new one.
  msg.Clear();
  DCHECK(!msg.has_request());

  // Read the 4-byte network-byte-order length.
  uint32_t len_nbo;
  Byte* len_buf = reinterpret_cast<Byte*>(&len_nbo);
  const ssize_t len = channel_->Read(len_buf, sizeof(len_nbo));
  if ABSL_PREDICT_FALSE (std::cmp_not_equal(len, sizeof(len_nbo))) {
    LOG(WARNING) << "failed to read control msg len: " << len;
    return false;
  }

  // Read the control message payload.
  Byte buf[ControlMsg::kMaxLen];
  const uint32_t size = ntohl(len_nbo);
  if ABSL_PREDICT_FALSE (size > ControlMsg::kMaxLen) {
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
  return ControlMsg::Deserialize(s, msg);
}

}  // namespace peregrine::internal
