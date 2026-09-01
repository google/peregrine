#ifndef PEREGRINE_SRC_INTERNAL_SOCKET_PSP_TCP_PSP_HELPER_H_
#define PEREGRINE_SRC_INTERNAL_SOCKET_PSP_TCP_PSP_HELPER_H_

#include <cstdint>
#include <string>

#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "src/internal/base/types.h"

namespace peregrine::internal {

// Sets mock PSP syscalls for testing.
void SetPspTcpSyscallsForTesting(void* psp_sys);

// Structure representing an exchanged PSP key.
struct PspSpiKey {
  uint32_t spi = 0;
  std::string key;

  // Returns true iff the PSP SPI and key are valid.
  bool IsValid() const { return spi != 0 && key.size() == 16; }
};

// Returns true if PSP-TCP is supported.
bool IsPspSupported();

// Registers a client's PSP key on server_fd and returns the allocated server
// RX key.
absl::StatusOr<PspSpiKey> RegisterPspPeerKey(fd_t server_fd,
                                             const PspSpiKey& client_key);

// Returns true if the accepted socket has valid PSP encryption active
// according to GetInitialRxSpi.
bool PspEnabled(fd_t client_fd);

// Allocates a fresh RX SPI and 16-byte key on client sock_fd.
absl::StatusOr<PspSpiKey> AcquireRxSpiAndKey(fd_t sock_fd);

// Sets the server transmit key on client sock_fd before connecting.
absl::Status SetTxSpiAndKey(fd_t sock_fd, const PspSpiKey& server_key);

// Returns the initial negotiated RX SPI on sock_fd.
absl::StatusOr<uint32_t> GetInitialRxSpi(fd_t sock_fd);

}  // namespace peregrine::internal

#endif  // PEREGRINE_SRC_INTERNAL_SOCKET_PSP_TCP_PSP_HELPER_H_
