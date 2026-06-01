#ifndef PEREGRINE_SRC_INTERNAL_SOCKET_SOCKET_UTIL_H_
#define PEREGRINE_SRC_INTERNAL_SOCKET_SOCKET_UTIL_H_

#include <sys/socket.h>

#include <cerrno>
#include <cstring>
#include <string>
#include <string_view>

#include "absl/strings/str_cat.h"
#include "absl/strings/str_format.h"

namespace peregrine::internal {

// Creates a new socket.
// Returns its file descriptor if successful, or -1 otherwise.
int CreateSocket(int family, int type, bool nonblocking);

// Returns a self ip:port string for the socket `fd`.
std::string SelfAddrPort(int fd);

// Returns a peer ip:port string for the socket `fd`.
std::string PeerAddrPort(int fd);

// Returns a string of self/peer ip:port pair for the socket `fd`.
inline std::string AddrPortPair(int fd) {
  return absl::StrCat(SelfAddrPort(fd), " <> ", PeerAddrPort(fd));
}

// Sets socket option. Returns true if successful, false otherwise.
inline bool SetOption(int fd, int opt, const void* val, socklen_t len) {
  return setsockopt(fd, SOL_SOCKET, opt, val, len) >= 0;
}

// Returns true iff the socket `fd` is in blocking mode.
bool IsBlockingMode(int fd);

// Returns true iff the socket `fd` is in non-blocking mode.
bool IsNonBlockingMode(int fd);

// Sets the socket to the specified blocking mode.
// Returns true if successful, false otherwise.
// For internal use only.
bool __set_blocking_mode(int fd, bool nonblocking);

// Sets the socket to blocking mode.
// Returns true if successful, false otherwise.
inline bool SetBlockingMode(int fd) {
  return __set_blocking_mode(fd, /*nonblocking=*/false);
}

// Sets the socket to non-blocking mode.
// Returns true if successful, false otherwise.
inline bool SetNonBlockingMode(int fd) {
  return __set_blocking_mode(fd, /*nonblocking=*/true);
}

// Returns true iff the last socket operation was interrupted by a signal.
inline bool Interrupted() { return errno == EINTR; }

// Returns true iff the last socket operation would block.
inline bool WouldBlock() { return errno == EAGAIN || errno == EWOULDBLOCK; }

// Returns true iff the socket connect operation is in progress.
inline bool InProgress() { return errno == EINPROGRESS; }

// Returns a success message for the last socket operation.
inline std::string SuccessMsg(std::string_view who, std::string_view what,
                              int fd) {
  return absl::StrCat(who, " socket ", what, ", fd=", fd, " ",
                      AddrPortPair(fd));
}

// Returns a success message for the last socket send/recv call.
inline std::string SuccessMsg(std::string_view who, std::string_view what,
                              int fd, size_t bytes) {
  return absl::StrCat(who, " socket ", what, ", fd=", fd, " ", AddrPortPair(fd),
                      " #bytes=", bytes);
}

// Returns an error message for the last socket operation.
inline std::string ErrorMsg(std::string_view who, std::string_view what,
                            int fd) {
  const auto last_errno = errno;
  return absl::StrFormat("%s socket %s failed: fd=%d %s errno=%d (%s)", who,
                         what, fd, AddrPortPair(fd), last_errno,
                         std::strerror(last_errno));
}

// Returns an error message for the last socket operation.
inline std::string ErrorMsg(std::string_view what) {
  const auto last_errno = errno;
  return absl::StrFormat("socket %s failed: errno=%d (%s)", what, last_errno,
                         std::strerror(last_errno));
}

}  // namespace peregrine::internal

#endif  // PEREGRINE_SRC_INTERNAL_SOCKET_SOCKET_UTIL_H_
