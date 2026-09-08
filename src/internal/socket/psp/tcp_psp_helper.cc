#include "src/internal/socket/psp/tcp_psp_helper.h"

#include <atomic>
#include <cstdint>
#include <cstring>
#include <memory>
#include <optional>
#include <string>

#include "absl/log/check.h"
#include "absl/log/log.h"
#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/strings/string_view.h"
#include "src/internal/base/types.h"

namespace peregrine::internal {

void TestOnly_SetPspTcpSyscalls(void* psp_sys) {}

bool IsPspSupported() { return false; }

bool IsPspEnabled(fd_t fd) { return false; }

absl::StatusOr<uint32_t> GetInitialRxSpi(fd_t fd) {
  return absl::UnimplementedError("psp not supported");
}

absl::StatusOr<PspToken> AcquireRxSpiAndKey(fd_t fd) {
  return absl::UnimplementedError("psp not supported");
}

absl::Status SetTxSpiAndKey(fd_t fd, const PspToken& token) {
  return absl::UnimplementedError("psp not supported");
}

absl::StatusOr<PspToken> RegisterPeerPsp(fd_t fd, const PspToken& token) {
  return absl::UnimplementedError("psp not supported");
}

}  // namespace peregrine::internal
