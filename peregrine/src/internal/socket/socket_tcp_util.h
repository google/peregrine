#ifndef PEREGRINE_SRC_INTERNAL_SOCKET_SOCKET_TCP_UTIL_H_
#define PEREGRINE_SRC_INTERNAL_SOCKET_SOCKET_TCP_UTIL_H_

namespace peregrine::internal {

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

#endif  // PEREGRINE_SRC_INTERNAL_SOCKET_SOCKET_TCP_UTIL_H_
