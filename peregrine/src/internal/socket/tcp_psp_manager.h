#ifndef PEREGRINE_SRC_INTERNAL_SOCKET_TCP_PSP_MANAGER_H_
#define PEREGRINE_SRC_INTERNAL_SOCKET_TCP_PSP_MANAGER_H_

#include <memory>

#include "absl/functional/any_invocable.h"
#include "absl/status/statusor.h"
#include "peregrine/src/internal/base/endpoint.h"
#include "peregrine/src/internal/socket/psp/psp.h"
#include "peregrine/src/internal/socket/socket_tcp.h"
#include "peregrine/src/internal/socket/tcp_manager.h"

namespace peregrine::internal {

// This utility class adds PSP functionality to the `TcpManager`.
// It must not have any data members.
class TcpPspManager : public TcpManager {
 public:
  using PspTokenExchange = absl::AnyInvocable<absl::StatusOr<PspToken>(
      const PspToken& self_token, const Endpoint& peer,
      const Endpoint& peer_control)>;

  // Connects the `self` endpoint to the `peer` in blocking mode, while
  // exchanging PSP tokens with the `peer_control` endpoint. If `self` has
  // nonzero ip address, binds to it before connecting. Returns a connected
  // psp tcp socket if successful. Otherwise, returns a null pointer.
  static std::unique_ptr<TcpSocket> ConnectPsp(
      const Endpoint& self, const Endpoint& peer, const Endpoint& peer_control,
      PspTokenExchange& psp_token_xchg);

  // Handles peer psp token exchange request for the `self_target` endpoint.
  static absl::StatusOr<PspToken> ExchangePspTokens(
      const TcpManager& tcp_mgr, const PspToken& peer_token,
      const Endpoint& self_target);

  absl::StatusOr<PspToken> ExchangePspTokens(
      const PspToken& peer_token, const Endpoint& self_target) const {
    return ExchangePspTokens(*this, peer_token, self_target);
  }
};

}  // namespace peregrine::internal

#endif  // PEREGRINE_SRC_INTERNAL_SOCKET_TCP_PSP_MANAGER_H_
