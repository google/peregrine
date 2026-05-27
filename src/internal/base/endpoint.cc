#include "src/internal/base/endpoint.h"

#include <cstdint>
#include <string>

#include "absl/log/check.h"
#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/strings/numbers.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/string_view.h"
#include "src/internal/socket/ip_util.h"

namespace peregrine {

namespace {
absl::Status InvalidArgumentError(const absl::string_view msg,
                                  const absl::string_view arg) {
  return absl::InvalidArgumentError(absl::StrCat(msg, " ", arg));
}

bool LooksLikeIPv6(absl::string_view addr) {
  return addr.starts_with('[') && addr.ends_with(']');
}
}  // namespace

absl::StatusOr<Endpoint> Endpoint::Create(const absl::string_view ipaddr_port) {
  // "127.0.0.1:12345" or "[::1]:12345", or sth invalid
  const auto pos = ipaddr_port.rfind(':');
  if (pos == absl::string_view::npos) {
    return InvalidArgumentError("invalid", ipaddr_port);
  }

  const absl::string_view a = ipaddr_port.substr(0, pos);
  const absl::string_view p = ipaddr_port.substr(pos + 1);

  uint16_t port;
  if (!(absl::SimpleAtoi(p, &port) && 1 <= port && port <= 65535)) {
    return InvalidArgumentError("invalid port in ", ipaddr_port);
  }

  absl::string_view ipaddr = a;
  if (LooksLikeIPv6(ipaddr)) {
    ipaddr = a.substr(1, a.size() - 2);  // "[...]" -> "..."
    if (!IsIPv6Addr(ipaddr)) {
      return InvalidArgumentError("invalid ipv6 addr in ", ipaddr_port);
    }
  } else {
    if (!IsIPv4Addr(ipaddr)) {
      return InvalidArgumentError("invalid ipv4 addr in ", ipaddr_port);
    }
  }

  return Endpoint(ipaddr, port);
}

std::string Endpoint::ToString() const {
  DCHECK(IsValid());
  if (IsIPv4Addr(ipaddr_)) {
    return absl::StrCat(ipaddr_, ":", port_);
  } else {
    return absl::StrCat("[", ipaddr_, "]:", port_);
  }
};

}  // namespace peregrine
