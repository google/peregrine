#ifndef PEREGRINE_SRC_INTERNAL_SOCKET_SOCKET_UTIL_H_
#define PEREGRINE_SRC_INTERNAL_SOCKET_SOCKET_UTIL_H_

#include <sys/socket.h>

#include <cerrno>
#include <string>

#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/strings/str_cat.h"

namespace peregrine::internal {

// Creates a new socket.
// Returns its file descriptor if successful, or an error status otherwise.
absl::StatusOr<int> CreateSocket(int family, int type, bool nonblocking);

// Returns a self ip:port string for the socket `fd`.
std::string SelfAddrPort(int fd);

// Returns a peer ip:port string for the socket `fd`.
std::string PeerAddrPort(int fd);

// Returns a string of self/peer ip:port pair for the socket `fd`.
inline std::string AddrPortPair(int fd) {
  return absl::StrCat(SelfAddrPort(fd), " <> ", PeerAddrPort(fd));
}

// Sets socket option.
absl::Status SetOption(int fd, int optname, const void* optval,
                       socklen_t optlen);

// Returns true iff the socket `fd` is in blocking mode.
bool IsBlockingMode(int fd);

// Returns true iff the socket `fd` is in non-blocking mode.
bool IsNonBlockingMode(int fd);

// Sets the socket to blocking mode.
absl::Status SetBlockingMode(int fd);

// Sets the socket to non-blocking mode.
absl::Status SetNonBlockingMode(int fd);

// Returns true iff the last socket operation was interrupted by a signal.
inline bool Interrupted() { return errno == EINTR; }

// Returns true iff the last socket operation would block.
inline bool WouldBlock() { return errno == EAGAIN || errno == EWOULDBLOCK; }

// Returns true iff the socket connect operation is in progress.
inline bool InProgress() { return errno == EINPROGRESS; }

}  // namespace peregrine::internal

#endif  // PEREGRINE_SRC_INTERNAL_SOCKET_SOCKET_UTIL_H_
