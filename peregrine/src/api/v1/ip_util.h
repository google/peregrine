#ifndef PEREGRINE_SRC_API_V1_IP_UTIL_H_
#define PEREGRINE_SRC_API_V1_IP_UTIL_H_

#include <cstddef>
#include <string>
#include <string_view>

#include "absl/log/log.h"
#include "peregrine/src/internal/base/types.h"
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

// Returns true iff the environment variable `name` is set to a truthy value
// case-insensitively: {"1", "true", "yes", "on"}.
inline bool IsEnvVarEnabled(const std::string& name) {
  return util::IsEnvVarEnabled(name);
}

// Returns true iff the environment variable `name` is set to a falsy value
// case-insensitively: {"0", "false", "no", "off"}.
inline bool IsEnvVarDisabled(const std::string& name) {
  return util::IsEnvVarDisabled(name);
}

// Whether to pin the client-side source address of outbound data connections
// (env TPU_RAIDEN_ENABLE_SOURCE_IP_BIND is "1", "true", "yes" or "on").
//
// Binding a source IP only steers egress onto a particular NIC when the host
// has a policy routing rule for that address (`ip rule from <src> ...`). That
// setup exists on multi-NIC deployments where peer addresses are not covered
// by specific routes and would otherwise all leave via the control-plane NIC.
//
// Without such a rule the kernel still routes by destination, so the bound
// address disagrees with the interface the packet leaves on, and cloud
// anti-spoofing filters drop it. bind() itself succeeds and only the data path
// breaks, so the failure surfaces as a hang rather than an error. Callers must
// therefore only bind a source when the kernel's own route lookup confirms
// that the source's NIC is the egress for that peer (SourceIpRoutesToPeer).
// Binding stays opt-in because the policy routing that makes it useful is a
// property of the host.
inline bool SourceBindEnabled() {
  return IsEnvVarEnabled("TPU_RAIDEN_ENABLE_SOURCE_IP_BIND");
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
absl::StatusOr<bool> SourceIpRoutesToPeer(std::string_view source_ip,
                                          std::string_view peer);

// Test-only: drops every memoized SourceIpRoutesToPeer result.
void TestOnly_ClearRouteCache();

// Test-only: number of memoized SourceIpRoutesToPeer results.
size_t TestOnly_RouteCacheSize();

}  // namespace peregrine::v1

#endif  // PEREGRINE_SRC_API_V1_IP_UTIL_H_
