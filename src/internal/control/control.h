#ifndef PEREGRINE_SRC_INTERNAL_CONTROL_CONTROL_H_
#define PEREGRINE_SRC_INTERNAL_CONTROL_CONTROL_H_

#include <deque>
#include <memory>
#include <thread>  // NOLINT

#include "absl/base/thread_annotations.h"
#include "absl/synchronization/mutex.h"
#include "src/internal/channel/channel.h"
#include "src/internal/control/message.pb.h"
#include "src/util/macro.h"

namespace peregrine::internal {

// This class implements the transport control plane for endpoints to exchange
// control messages, such as request, host/device info, heartbeat, etc.
// It is thread-safe.
class Control final {
 public:
  // Constructor.
  explicit Control(std::unique_ptr<Channel> channel);

  // Disallows copy and move.
  DISALLOW_COPY(Control);
  DISALLOW_MOVE(Control);

  // Destructor.
  ~Control();

  // Enqueues a control message for sending.
  // Returns true iff the message is enqueued successfully.
  bool EnqueueSend(const proto::Control& c);

  // Dequeues a received control message.
  // Returns true iff a message is dequeued.
  bool DequeueRecv(proto::Control& c);

 private:
  // Returns true iff there are pending messages to send or the destructor is
  // called.
  bool hasSendWork() const ABSL_EXCLUSIVE_LOCKS_REQUIRED(mu_);

  // Runs in a thread to send control messages.
  void sendLoop();

  // Runs in a thread to receive control messages.
  void recvLoop();

 private:
  // Sends a control message to the `channel_`.
  // Returns true iff the message is sent successfully.
  bool send(const proto::Control& msg);

  // Receives a control message from the `channel_`.
  // Returns true iff the message is received successfully.
  bool recv(proto::Control& msg);

 private:
  mutable absl::Mutex mu_;
  bool stopping_ ABSL_GUARDED_BY(mu_);
  std::deque<proto::Control> send_queue_ ABSL_GUARDED_BY(mu_);
  std::deque<proto::Control> recv_queue_ ABSL_GUARDED_BY(mu_);

  std::unique_ptr<Channel> channel_;
  std::jthread send_thread_;
  std::jthread recv_thread_;
};

}  // namespace peregrine::internal

#endif  // PEREGRINE_SRC_INTERNAL_CONTROL_CONTROL_H_
