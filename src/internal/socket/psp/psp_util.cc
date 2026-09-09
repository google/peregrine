#include "src/internal/socket/psp/psp_util.h"

#include <array>
#include <atomic>
#include <cstring>
#include <memory>
#include <optional>

#include "absl/log/check.h"
#include "absl/log/log.h"
#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/strings/string_view.h"
#include "src/internal/base/types.h"
#include "src/internal/socket/psp/psp.h"

namespace peregrine::internal::psp {

void TestOnly_SetPspTcpSyscalls(void* psp_sys) {}

bool IsPspSupported() { return false; }

bool IsPspEnabled(fd_t fd) { return false; }

absl::StatusOr<Spi> GetInitialRxSpi(fd_t fd) {
  return absl::UnimplementedError("psp not supported");
}

absl::StatusOr<PspToken> AcquireRxSpiAndKey(fd_t fd) {
  return absl::UnimplementedError("psp not supported");
}

absl::Status SetTxSpiAndKey(fd_t fd, const PspToken& token) {
  return absl::UnimplementedError("psp not supported");
}

absl::StatusOr<PspToken> AddSecureListener(fd_t fd, const PspToken& token) {
  return absl::UnimplementedError("psp not supported");
}

absl::Status RemoveSecureListener(const fd_t fd, const PspToken& token) {
  return absl::UnimplementedError("psp not supported");
}
}  // namespace peregrine::internal::psp
