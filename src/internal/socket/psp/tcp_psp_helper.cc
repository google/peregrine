#include "src/internal/socket/psp/tcp_psp_helper.h"

#include <cstdint>
#include <memory>
#include <string>

#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/strings/string_view.h"
#include "src/internal/base/types.h"

namespace peregrine::internal {

void SetPspTcpSyscallsForTesting(void* psp_sys) {}

bool IsPspSupported() { return false; }

absl::StatusOr<PspSpiKey> RegisterPspPeerKey(
    fd_t server_fd, const PspSpiKey& client_key) {
  return absl::UnimplementedError("PSP is not supported");
}

absl::StatusOr<uint32_t> GetInitialRxSpi(fd_t sock_fd) {
  return absl::UnimplementedError("PSP is not supported");
}

bool PspEnabled(fd_t client_fd) { return false; }

absl::StatusOr<PspSpiKey> AcquireRxSpiAndKey(fd_t sock_fd) {
  return absl::UnimplementedError("PSP is not supported");
}

absl::Status SetTxSpiAndKey(fd_t sock_fd, const PspSpiKey& server_key) {
  return absl::UnimplementedError("PSP is not supported");
}

}  // namespace peregrine::internal
