#include "peregrine/src/internal/channel/channel_grpc.h"

#include <sys/types.h>

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <memory>
#include <string>
#include <vector>

#include "absl/base/optimization.h"
#include "absl/log/check.h"
#include "absl/log/log.h"
#include "absl/strings/str_cat.h"
#include "absl/synchronization/mutex.h"
#include "absl/synchronization/notification.h"
#include "grpcpp/support/byte_buffer.h"
#include "grpcpp/support/slice.h"
#include "peregrine/src/internal/base/types.h"
#include "peregrine/src/internal/lib/iovec_cursor.h"
#include "peregrine/src/internal/socket/grpc_manager.h"

namespace peregrine::internal {

namespace {
// Copies the remaining buffers of the `iovecs` into a gRPC message.
grpc::ByteBuffer ToMessage(const IoVecCursor& iovecs) {
  std::vector<grpc::Slice> slices;
  slices.reserve(iovecs.RemainingItems());
  for (const IoVec& iov : iovecs.RemainingSpan()) {
    slices.emplace_back(iov.iov_base, iov.iov_len);
  }
  return grpc::ByteBuffer(slices.data(), slices.size());
}

// Copies the gRPC message `msg` into the remaining buffers of the `iovecs`,
// and advances the `iovecs` by the message size.
// Returns the message size, or -1 if the message is bigger than the buffers.
ssize_t FromMessage(const grpc::ByteBuffer& msg, IoVecCursor& iovecs) {
  const size_t len = msg.Length();
  if (len == 0) return 0;
  if ABSL_PREDICT_FALSE (len > iovecs.RemainingBytes()) return -1;

  std::vector<grpc::Slice> slices;
  if ABSL_PREDICT_FALSE (!msg.Dump(&slices).ok()) return -1;
  for (const grpc::Slice& slice : slices) {
    const uint8_t* src = slice.begin();
    size_t left = slice.size();
    while (left > 0) {
      const IoVec* const iov = iovecs.Head();
      const size_t n = std::min(left, iov->iov_len);
      std::memcpy(iov->iov_base, src, n);
      iovecs.Advance(n);
      src += n;
      left -= n;
    }
  }
  return len;
}
}  // namespace

bool GrpcChannel::canWrite() const { return !sending_ || shutdown_; }

ssize_t GrpcChannel::Write(IoVecCursor& iovecs) {
  DCHECK(iovecs.IsValid());

  const size_t len = iovecs.RemainingBytes();
  DCHECK_GE(len, 1);
  if ABSL_PREDICT_FALSE (len > kMaxMessageSize) {
    LOG(ERROR) << "message too big to write: " << len;
    return -1;
  }

  {
    // The stream sends one message at a time.
    absl::MutexLock lock(mu_);
    mu_.Await(absl::Condition(this, &GrpcChannel::canWrite));
    if (shutdown_ || send_failed_) return -1;
    sending_ = true;
  }

  // The send is started without `mu_`, because its callback takes `mu_` and
  // can run inside the call.
  grpc::ByteBuffer msg = ToMessage(iovecs);
  stream_->Send(&msg, [this](bool ok) {
    absl::MutexLock lock(mu_);
    sending_ = false;
    send_failed_ = !ok;
  });
  iovecs.Advance(len);
  return len;
}

ssize_t GrpcChannel::Read(IoVecCursor& iovecs) {
  DCHECK(iovecs.IsValid());
  DCHECK_GE(iovecs.RemainingBytes(), 1);

  grpc::ByteBuffer msg;
  bool rcvd = false;
  absl::Notification done;
  stream_->Recv([&msg, &rcvd, &done](bool ok, grpc::ByteBuffer* m) {
    msg.Swap(m);
    rcvd = ok;
    done.Notify();
  });

  done.WaitForNotification();
  if (!rcvd) return 0;

  const ssize_t len = FromMessage(msg, iovecs);
  if ABSL_PREDICT_FALSE (len < 0) {
    LOG(ERROR) << "message too big to read: " << msg.Length() << " > "
               << iovecs.RemainingBytes();
  }
  return len;
}

void GrpcChannel::Shutdown() {
  {
    absl::MutexLock lock(mu_);
    shutdown_ = true;
  }
  // The stream is shut down without `mu_`, because the callback of the send
  // in progress takes `mu_` and can run inside the call.
  stream_->Shutdown();
}

std::string GrpcChannel::ToString() const {
  return absl::StrCat("GrpcChannel: ", stream_->ToString());
}

}  // namespace peregrine::internal
