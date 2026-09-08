#ifndef PEREGRINE_SRC_INTERNAL_SOCKET_PSP_PSP_UTIL_H_
#define PEREGRINE_SRC_INTERNAL_SOCKET_PSP_PSP_UTIL_H_

#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "src/internal/base/types.h"
#include "src/internal/socket/psp/psp.h"

namespace peregrine::internal::psp {

// Sets mock psp syscalls for testing.
void TestOnly_SetPspTcpSyscalls(void* psp_sys);

// Returns true iff psp is supported.
bool IsPspSupported();

// Returns true iff the socket has psp encryption enabled.
bool IsPspEnabled(fd_t fd);

// Returns the initial rx psp spi/key on the socket `fd`.
absl::StatusOr<Spi> GetInitialRxSpi(fd_t fd);

// Gets rx psp spi/key on the socket `fd`.
absl::StatusOr<PspToken> AcquireRxSpiAndKey(fd_t fd);

// Sets tx psp spi/key on the socket `fd`.
absl::Status SetTxSpiAndKey(fd_t fd, const PspToken& token);

// Registers a peer's psp token on the listening socket `fd`.
// Returns this socket's psp token if successful, otherwise returns an error.
absl::StatusOr<PspToken> AddSecureListener(fd_t fd, const PspToken& token);

// Removes a secure listener from the listening socket `fd`.
absl::Status RemoveSecureListener(fd_t fd, const PspToken& token);

}  // namespace peregrine::internal::psp

#endif  // PEREGRINE_SRC_INTERNAL_SOCKET_PSP_PSP_UTIL_H_
