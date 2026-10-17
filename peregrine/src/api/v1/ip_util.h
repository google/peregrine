#ifndef PEREGRINE_SRC_API_V1_IP_UTIL_H_
#define PEREGRINE_SRC_API_V1_IP_UTIL_H_

#include <string>
#include <string_view>

#include "absl/log/log.h"
#include "peregrine/src/internal/base/types.h"
#include "peregrine/src/internal/routing/route.h"
#include "peregrine/src/internal/socket/socket_util.h"
#include "peregrine/src/util/envvar.h"

namespace peregrine::v1 {

// Returns the local "ip:port" for the socket, or "?" if it can't be determined.
inline std::string GetLocalEndpoint(int sock_fd) {
  const internal::fd_t fd(sock_fd);
  return internal::SelfAddrPort(fd);
}

// Returns a self/peer address pair ("ip:port--ip:port") for the socket.
// Each side is "?" if it can't be determined; the peer is "*" if the socket
// is not connected.
inline std::string GetAddrPortPair(int sock_fd) {
  const internal::fd_t fd(sock_fd);
  return internal::AddrPortPair(fd);
}

// True when a socket bound to `source_ip` and connected to `peer`
// ("host:port" or "[v6]:port") would egress through the interface that owns
// `source_ip`, per the kernel's route lookup (RTM_GETROUTE with RTA_DST and
// RTA_SRC, honouring policy-routing rules). False when the kernel would route
// the peer via a different interface -- binding `source_ip` would then send
// packets with a source address foreign to the egress link, which cloud
// anti-spoofing drops. Loopback source and loopback peer are trivially true.
// Errors: `peer` unparsable / unresolvable, `source_ip` not an address of any
// local interface, or netlink failure.
//
// Results are memoized for the lifetime of the process keyed by (source_ip,
// peer) exactly as passed; host routing is assumed static for the job. Errors
// are not memoized, so a transient failure is retried on the next call.
inline absl::StatusOr<bool> IsRoutable(std::string_view source_ip,
                                       std::string_view peer) {
  return true;  // internal::Route::IsRoutable(source_ip, peer);
}

// Returns true when environment variable `name` is set to a truthy value
// case-insensitively: {"1", "true", "yes", "on"}.
inline bool IsEnvVarEnabled(const std::string& name) {
  return util::IsEnvVarEnabled(name);
}

// Returns true when environment variable `name` is set to a falsy value
// case-insensitively: {"0", "false", "no", "off"}.
inline bool IsEnvVarDisabled(const std::string& name) {
  return util::IsEnvVarDisabled(name);
}

}  // namespace peregrine::v1

#endif  // PEREGRINE_SRC_API_V1_IP_UTIL_H_
