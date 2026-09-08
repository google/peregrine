#ifndef PEREGRINE_SRC_INTERNAL_SOCKET_PSP_TCP_PSP_HELPER_H_
#define PEREGRINE_SRC_INTERNAL_SOCKET_PSP_TCP_PSP_HELPER_H_

#include <cstddef>
#include <cstdint>
#include <ostream>
#include <string>

#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/strings/escaping.h"
#include "absl/strings/str_format.h"
#include "src/internal/base/types.h"

namespace peregrine::internal {

constexpr size_t kPspKeyLen = 16;

// This struct defines a psp token.
struct PspToken final {
  uint32_t spi;
  std::string key;

  // Returns true iff the psp token is valid.
  bool IsValid() const { return spi != 0 && key.size() == kPspKeyLen; }

  // Returns a string representation of the psp token.
  std::string ToString() const {
    return absl::StrFormat("PspToken(spi=%u, key=0x%s)", spi,
                           absl::BytesToHexString(key));
  }
};

inline std::ostream& operator<<(std::ostream& os, const PspToken& token) {
  return os << token.ToString();
}

// Sets mock psp syscalls for testing.
void TestOnly_SetPspTcpSyscalls(void* psp_sys);

// Returns true iff psp is supported.
bool IsPspSupported();

// Returns true iff the socket has psp encryption enabled.
bool IsPspEnabled(fd_t fd);

// Returns the initial rx psp spi/key on the socket `fd`.
absl::StatusOr<uint32_t> GetInitialRxSpi(fd_t fd);

// Gets rx psp spi/key on the socket `fd`.
absl::StatusOr<PspToken> AcquireRxSpiAndKey(fd_t fd);

// Sets tx psp spi/key on the socket `fd`.
absl::Status SetTxSpiAndKey(fd_t fd, const PspToken& token);

// Registers a peer's psp token on the socket `fd`.
// Returns this socket's psp token if successful, otherwise returns an error.
absl::StatusOr<PspToken> RegisterPeerPsp(fd_t fd, const PspToken& token);

}  // namespace peregrine::internal

#endif  // PEREGRINE_SRC_INTERNAL_SOCKET_PSP_TCP_PSP_HELPER_H_
