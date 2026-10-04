#ifndef PEREGRINE_SRC_INTERNAL_SOCKET_SOCKET_ERROR_H_
#define PEREGRINE_SRC_INTERNAL_SOCKET_SOCKET_ERROR_H_

#include <cerrno>

#include "peregrine/src/internal/base/types.h"
#include "peregrine/src/util/errno.h"

namespace peregrine::internal {

// Returns the last socket error code. Returns 0 if there is no error.
int GetSocketError(fd_t fd);

// Returns true iff the last socket operation was interrupted by a signal.
inline bool Interrupted(util::Errno err) { return err.value() == EINTR; }

// Returns true iff the last socket connect operation is in progress.
inline bool InProgress(util::Errno err) { return err.value() == EINPROGRESS; }

// Returns true iff the last socket operation would block.
inline bool WouldBlock(util::Errno err) {
  return err.value() == EAGAIN || err.value() == EWOULDBLOCK;
}

// Returns true iff the last socket operation failed due to resource exhaustion.
inline bool OutOfResource(util::Errno err) {
  return err.value() == EMFILE || err.value() == ENFILE ||
         err.value() == ENOBUFS || err.value() == ENOMEM;
}

// Return codes of the tcp listen socket Accept() call.
// A non-negative value is the file descriptor of the newly accepted socket.
inline constexpr int kAcceptError = -1;
inline constexpr int kAcceptWouldBlock = -2;
inline constexpr int kAcceptShutdown = -10;
inline constexpr int kAcceptOutOfResource = -11;

// Return codes of the tcp socket Connect() call.
inline constexpr int kConnectInProgress = 1;
inline constexpr int kConnectSuccess = 0;
inline constexpr int kConnectError = -1;

// Returns true iff the tcp listen socket Accept() call would block.
inline bool IsWouldBlock(int ret) { return ret == kAcceptWouldBlock; }

// Returns true iff the tcp listen socket Accept() call was shut down.
inline bool IsShutdown(int ret) { return ret == kAcceptShutdown; }

// Returns true iff the tcp listen socket Accept() call failed due to
// resource exhaustion.
inline bool IsOutOfResource(int ret) { return ret == kAcceptOutOfResource; }

}  // namespace peregrine::internal

#endif  // PEREGRINE_SRC_INTERNAL_SOCKET_SOCKET_ERROR_H_
