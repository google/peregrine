#include "src/api/transport_util.h"

#include <algorithm>
#include <cstdint>
#include <memory>
#include <optional>
#include <string_view>

#include "absl/base/optimization.h"
#include "absl/log/log.h"
#include "absl/strings/numbers.h"
#include "src/api/transport.h"
#include "src/internal/base/config.h"
#include "src/internal/base/endpoint.h"
#include "src/internal/base/ipaddr.h"
#include "src/internal/transport_impl.h"
#include "src/util/util.h"

namespace peregrine {

using internal::Config;
using internal::Endpoint;
using internal::IpAddr;
using internal::TransportImpl;

namespace {

// Parses the given `control_endpoint` string and returns the corresponding
// Endpoint. Returns an empty Endpoint on failure.
//
// As this is a control related function, we can move it to Control::Create()
// once Control is hooked up and fail there if the endpoint is invalid. This
// keeps the transport API simple and hides the internal details.
Endpoint ParseControlEndpoint(std::string_view control_endpoint) {
  const auto pos = control_endpoint.rfind(':');
  if (pos == std::string_view::npos) {
    LOG(WARNING) << "invalid control endpoint: " << control_endpoint;
    return {};
  }

  std::string_view ip_str = control_endpoint.substr(0, pos);
  const std::string_view port_str = control_endpoint.substr(pos + 1);

  // Strip IPv6 brackets if present.
  if (ip_str.starts_with('[') && ip_str.ends_with(']')) {
    ip_str = ip_str.substr(1, ip_str.size() - 2);
  }

  std::optional<IpAddr> ip = IpAddr::Create(ip_str);
  if (!ip.has_value()) {
    LOG(WARNING) << "failed to parse IP address in: " << control_endpoint;
    return {};
  }

  int port = -1;
  if (!absl::SimpleAtoi(port_str, &port) || port < 0 || port > 65535) {
    LOG(WARNING) << "invalid port in: " << control_endpoint;
    return {};
  }

  // Map wildcard IP addresses to loopback. This should be used for testing
  // only.
  // TODO: When we support interface enumeration, we should ensure that the
  // control endpoint corresponds to a routable address so peers can connect to
  // it.
  if (ip->IsZero()) {
    ip = ip->IsIPv6() ? IpAddr(*internal::ParseIPv6Addr("::1"))
                      : IpAddr(*internal::ParseIPv4Addr("127.0.0.1"));
  }

  // Find a free TCP port if port 0 was requested.
  if (port == 0) {
    const int family = ip->IsIPv6() ? AF_INET6 : AF_INET;
    const uint16_t free_port = util::FindFreePort(family, /*tcp=*/true);
    if (free_port == 0) {
      LOG(WARNING) << "failed to find a free port for control endpoint: "
                   << control_endpoint;
      return {};
    }
    port = free_port;
  }

  return Endpoint(*ip, port);
}

}  // namespace

std::unique_ptr<Transport> CreateTransport(std::string_view control_endpoint,
                                           int num_conns_per_peer) {
  const Endpoint ctrl_ep = ParseControlEndpoint(control_endpoint);
  if ABSL_PREDICT_FALSE (!ctrl_ep.HasNonzeroIpPort()) {
    return nullptr;
  }

  const int n = std::min(std::max(1, num_conns_per_peer), 100);
  const Config config = {.num_conns_per_peer = n};
  return TransportImpl::Create(config, ctrl_ep);
}

}  // namespace peregrine
