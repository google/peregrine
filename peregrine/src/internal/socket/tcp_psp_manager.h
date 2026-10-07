#ifndef PEREGRINE_SRC_INTERNAL_SOCKET_TCP_PSP_MANAGER_H_
#define PEREGRINE_SRC_INTERNAL_SOCKET_TCP_PSP_MANAGER_H_

#include <memory>
#include "absl/functional/any_invocable.h"
#include "absl/log/check.h"
#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "peregrine/src/internal/base/endpoint.h"
#include "peregrine/src/internal/base/types.h"
#include "peregrine/src/internal/socket/psp/psp.h"
#include "peregrine/src/internal/socket/socket_tcp.h"

namespace peregrine::internal {

// This utility class provides PSP support for `TcpManager`.
// It is thread-safe.
class PspTcpManager {
 public:
  using PspTokenExchange = absl::AnyInvocable<absl::StatusOr<PspToken>(
      const fd_t fd, const PspToken& peer_token, const Endpoint& self_target)>;

  static absl::Status PreConnect(fd_t fd, const Endpoint& peer,
                                 const Endpoint& peer_control,
                                 PspTokenExchange& psp_token_xchg);

  static absl::Status PostConnect(fd_t fd, const PspToken& self_token);

  // Handles peer psp token exchange request for the `self_target` endpoint.
  static absl::StatusOr<PspToken> ExchangePspTokens(
      fd_t fd, const PspToken& peer_token, const Endpoint& self_target);

  // Connects to the `peer` endpoint with the given `self` and `peer_control`
  // endpoints, and the `psp_token_xchg` for psp token exchange.
  // Returns nullptr on failure.
  std::unique_ptr<TcpSocket> ConnectPsp(const Endpoint& self,
                                        const Endpoint& peer,
                                        const Endpoint& peer_control,
                                        PspTokenExchange& psp_token_xchg)
};

}  // namespace peregrine::internal

#endif  // PEREGRINE_SRC_INTERNAL_SOCKET_TCP_PSP_MANAGER_H_
