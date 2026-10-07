#ifndef PEREGRINE_SRC_INTERNAL_SOCKET_SOCKET_UTIL_H_
#define PEREGRINE_SRC_INTERNAL_SOCKET_SOCKET_UTIL_H_

#include <fcntl.h>
#include <sys/socket.h>

#include <cstddef>
#include <cstring>
#include <string>
#include <string_view>

#include "absl/strings/str_cat.h"
#include "absl/strings/str_format.h"
#include "peregrine/src/internal/base/endpoint.h"
#include "peregrine/src/internal/base/types.h"
#include "peregrine/src/util/errno.h"

namespace peregrine::internal {

// Creates a new socket.
// Returns its file descriptor if successful, or -1 otherwise.
fd_t CreateSocket(int family, int type, bool blocking);

// Returns true iff the socket `fd` is valid (not closed).
inline bool IsValidSocket(fd_t fd) { return ::fcntl(fd.value(), F_GETFD) >= 0; }

// Sets socket option. Returns 0 on success, -1 otherwise.
inline int SetSocketOption(fd_t fd, int opt, const void* val, socklen_t len) {
  return ::setsockopt(fd.value(), SOL_SOCKET, opt, val, len);
}

// Gets socket option. Returns 0 on success, -1 otherwise.
inline int GetSocketOption(fd_t fd, int opt, void* val, socklen_t* len) {
  return ::getsockopt(fd.value(), SOL_SOCKET, opt, val, len);
}

// Returns true iff the socket `fd` is in blocking mode.
// Returning false does not mean the socket is in non-blocking mode.
// Use `IsNonBlockingMode()` to check.
bool IsBlockingMode(fd_t fd);

// Returns true iff the socket `fd` is in non-blocking mode.
// Returning false does not mean the socket is in blocking mode.
// Use `IsBlockingMode()` to check.
bool IsNonBlockingMode(fd_t fd);

// Returns true iff the socket `fd` is in the specified blocking mode.
inline bool MatchesBlockingMode(fd_t fd, bool blocking) {
  return blocking ? IsBlockingMode(fd) : IsNonBlockingMode(fd);
}

// Returns the address family of the socket `fd`.
sa_family_t AddrFamily(fd_t fd);

// Returns the self ip:port string for the socket `fd`.
std::string SelfAddrPort(fd_t fd);

// Returns the peer ip:port string for the socket `fd`.
std::string PeerAddrPort(fd_t fd);

// Returns the self endpoint for the socket `fd`.
Endpoint SelfEndpoint(fd_t fd);

// Returns the peer endpoint for the socket `fd`.
Endpoint PeerEndpoint(fd_t fd);

// Returns a string of self/peer ip:port pair for the socket `fd`.
inline std::string AddrPortPair(fd_t fd) {
  return absl::StrCat(SelfAddrPort(fd), "--", PeerAddrPort(fd));
}

// Returns a "ipv4:port" or "[ipv6]:port" string.
std::string ToIpAddrPortString(const struct sockaddr_storage& ss);

// Returns a success message for the last socket operation.
inline std::string SuccessMsg(std::string_view who, std::string_view what,
                              fd_t fd) {
  return absl::StrCat(who, " socket ", what, ", fd=", fd.value(), " ",
                      AddrPortPair(fd));
}

// Returns a success message for the last socket send/recv call.
inline std::string SuccessMsg(std::string_view who, std::string_view what,
                              fd_t fd, size_t bytes) {
  return absl::StrCat(who, " socket ", what, ", fd=", fd.value(), " ",
                      AddrPortPair(fd), " #bytes=", bytes);
}

// Returns an error message for the last socket operation.
inline std::string ErrorMsg(std::string_view who, std::string_view what,
                            fd_t fd, util::Errno err) {
  return absl::StrFormat("%s socket %s failed: fd=%d %s errno=%d (%s)", who,
                         what, fd.value(), AddrPortPair(fd), err.value(),
                         std::strerror(err.value()));
}

// Returns an error message for the last socket operation.
inline std::string ErrorMsg(std::string_view what, util::Errno err) {
  return absl::StrFormat("socket %s failed: errno=%d (%s)", what, err.value(),
                         std::strerror(err.value()));
}

}  // namespace peregrine::internal

#endif  // PEREGRINE_SRC_INTERNAL_SOCKET_SOCKET_UTIL_H_
