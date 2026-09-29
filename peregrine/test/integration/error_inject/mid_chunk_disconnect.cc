#include "peregrine/test/integration/error_inject/mid_chunk_disconnect.h"

#include <sys/socket.h>
#include <sys/types.h>
#include <sys/uio.h>

#include <atomic>
#include <cerrno>
#include <cstdint>
#include <memory>
#include <string>
#include <string_view>

#include "absl/flags/flag.h"
#include "absl/log/check.h"
#include "absl/log/log.h"
#include "absl/strings/match.h"
#include "peregrine/test/integration/error_inject/error_injector.h"

ABSL_FLAG(std::string, disconnect_mode, "eof",
          "TCP disconnect mode for mid_chunk_disconnect: 'eof' or 'rst'");

ABSL_FLAG(std::string, disconnect_point, "mid_payload",
          "Point within a chunk to disconnect: 'mid_header', 'after_header', "
          "or 'mid_payload'");

ABSL_FLAG(uint64_t, disconnect_on_chunk, 1,
          "1-based chunk sequence number on which to trigger a one-shot "
          "disconnect (0 disables one-shot trigger)");

ABSL_FLAG(uint64_t, disconnect_every_n_chunks, 0,
          "If > 0, trigger a disconnect every N chunks");

namespace peregrine::integration {

namespace {
MidChunkDisconnect::Mode ParseMode(std::string_view s) {
  if (absl::EqualsIgnoreCase(s, "eof")) {
    return MidChunkDisconnect::Mode::kEof;
  } else if (absl::EqualsIgnoreCase(s, "rst")) {
    return MidChunkDisconnect::Mode::kRst;
  } else {
    LOG(FATAL) << "invalid --disconnect_mode: " << s
               << ". Expected 'eof' or 'rst'.";
  }
}

MidChunkDisconnect::Point ParsePoint(std::string_view s) {
  if (absl::EqualsIgnoreCase(s, "mid_header")) {
    return MidChunkDisconnect::Point::kMidHeader;
  } else if (absl::EqualsIgnoreCase(s, "after_header")) {
    return MidChunkDisconnect::Point::kAfterHeader;
  } else if (absl::EqualsIgnoreCase(s, "mid_payload")) {
    return MidChunkDisconnect::Point::kMidPayload;
  } else {
    LOG(FATAL) << "invalid --disconnect_point: " << s
               << ". Expected 'mid_header', 'after_header', or 'mid_payload'.";
  }
}
}  // namespace

MidChunkDisconnect::MidChunkDisconnect(Mode mode, Point point,
                                       uint64_t on_chunk,
                                       uint64_t every_n_chunks)
    : mode_(mode),
      point_(point),
      on_chunk_(on_chunk),
      every_n_chunks_(every_n_chunks) {
  QCHECK(on_chunk_ > 0 || every_n_chunks_ > 0)
      << "At least one of --disconnect_on_chunk or --disconnect_every_n_chunks "
         "must be > 0";
}

std::unique_ptr<MidChunkDisconnect> MidChunkDisconnect::Create() {
  const Mode mode = ParseMode(absl::GetFlag(FLAGS_disconnect_mode));
  const Point point = ParsePoint(absl::GetFlag(FLAGS_disconnect_point));
  const uint64_t on_chunk = absl::GetFlag(FLAGS_disconnect_on_chunk);
  const uint64_t every_n_chunks =
      absl::GetFlag(FLAGS_disconnect_every_n_chunks);
  return std::make_unique<MidChunkDisconnect>(mode, point, on_chunk,
                                              every_n_chunks);
}

bool MidChunkDisconnect::shouldInject(uint64_t chunk_seq) const {
  if (on_chunk_ > 0 && chunk_seq == on_chunk_) {
    return true;
  }
  if (every_n_chunks_ > 0 && (chunk_seq % every_n_chunks_ == 0)) {
    return true;
  }
  return false;
}

void MidChunkDisconnect::injectDisconnect(int fd) const {
  if (mode_ == Mode::kRst) {
    struct sockaddr unspec = {};
    unspec.sa_family = AF_UNSPEC;
    ::connect(fd, &unspec, sizeof(unspec));
  } else {
    ::shutdown(fd, SHUT_RDWR);
  }
}

ssize_t MidChunkDisconnect::OnSendMsg(int fd, const struct msghdr* msg,
                                      int flags, SendMsgFn real_sendmsg) {
  // Match Peregrine data chunks sent by Transfer::SendChunk():
  // msg_iov[0] is the 64-byte ChunkHeader, msg_iov[1] is the chunk payload.
  if (msg == nullptr || msg->msg_iov == nullptr || msg->msg_iovlen != 2 ||
      msg->msg_iov[0].iov_len != kChunkHeaderSize) {
    return real_sendmsg(fd, msg, flags);
  }

  const uint64_t seq =
      chunk_count_.fetch_add(1, std::memory_order_relaxed) + 1;
  if (!shouldInject(seq)) {
    return real_sendmsg(fd, msg, flags);
  }

  const uint64_t injected =
      injected_count_.fetch_add(1, std::memory_order_relaxed) + 1;
  LOG(WARNING) << "Injecting mid-chunk TCP disconnect (#" << injected
               << ") on fd=" << fd << " at chunk #" << seq;

  struct msghdr partial_msg = *msg;
  struct iovec partial_iov[2];
  partial_msg.msg_iov = partial_iov;

  switch (point_) {
    case Point::kMidHeader:
      partial_iov[0].iov_base = msg->msg_iov[0].iov_base;
      partial_iov[0].iov_len = kChunkHeaderSize / 2;
      partial_msg.msg_iovlen = 1;
      break;
    case Point::kAfterHeader:
      partial_iov[0] = msg->msg_iov[0];
      partial_msg.msg_iovlen = 1;
      break;
    case Point::kMidPayload:
      partial_iov[0] = msg->msg_iov[0];
      partial_iov[1].iov_base = msg->msg_iov[1].iov_base;
      partial_iov[1].iov_len = msg->msg_iov[1].iov_len / 2;
      partial_msg.msg_iovlen = (partial_iov[1].iov_len > 0) ? 2 : 1;
      break;
  }

  real_sendmsg(fd, &partial_msg, flags);
  injectDisconnect(fd);
  errno = (mode_ == Mode::kRst) ? ECONNRESET : EPIPE;
  return -1;
}

}  // namespace peregrine::integration
