#ifndef PEREGRINE_SRC_INTERNAL_SOCKET_SOCKET_ERROR_H_
#define PEREGRINE_SRC_INTERNAL_SOCKET_SOCKET_ERROR_H_

#include <fcntl.h>
#include <sys/socket.h>

#include <cerrno>

#include "peregrine/src/internal/base/types.h"

namespace peregrine::internal {

// Returns the last socket error code. Returns 0 if there is no error.
int GetSocketError(fd_t fd);

// Returns true iff the last socket operation was interrupted by a signal.
inline bool Interrupted(int last_errno) { return last_errno == EINTR; }

// Returns true iff the last socket connect operation is in progress.
inline bool InProgress(int last_errno) { return last_errno == EINPROGRESS; }

// Returns true iff the last socket operation would block.
inline bool WouldBlock(int last_errno) {
  return last_errno == EAGAIN || last_errno == EWOULDBLOCK;
}

// Returns true iff the last socket operation failed due to resource exhaustion.
inline bool OutOfResource(int last_errno) {
  return last_errno == EMFILE || last_errno == ENFILE ||
         last_errno == ENOBUFS || last_errno == ENOMEM;
}

// Return codes of the tcp listen socket Accept() call.
// A non-negative value is the file descriptor of the newly accepted socket.
inline constexpr int kAcceptError = -1;
inline constexpr int kAcceptWouldBlock = -2;
inline constexpr int kAcceptShutdown = -10;
inline constexpr int kAcceptOutOfResource = -11;

// Return codes of the tcp socket Connect() call.
inline constexpr int kConnectInProgress = 1;
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
