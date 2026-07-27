#ifndef PEREGRINE_SRC_INTERNAL_BASE_IPADDR_H_
#define PEREGRINE_SRC_INTERNAL_BASE_IPADDR_H_

#include <netinet/in.h>
#include <sys/types.h>
#include <sys/uio.h>

#include <cstddef>
#include <optional>
#include <ostream>
#include <string>
#include <string_view>
#include <variant>

#include "absl/hash/hash.h"
#include "absl/log/check.h"
#include "absl/types/span.h"
#include "src/internal/assumptions.h"
#include "src/util/macro.h"

namespace peregrine::internal {

// ip v{4,6} addresses
using ipv4_t = ::in_addr;
using ipv6_t = ::in6_addr;

// Parses the `ip` address string. Returns an `in_addr` struct if successful.
// Otherwise, returns `std::nullopt`.
std::optional<ipv4_t> ParseIPv4Addr(std::string_view ip);

// Parses the `ip` address string. Returns an `in6_addr` struct if successful.
// Otherwise, returns `std::nullopt`.
std::optional<ipv6_t> ParseIPv6Addr(std::string_view ip);

// Returns a string representation of the ipv4 address.
std::string ToIPv4String(const ipv4_t& ip4);

// Returns a string representation of the ipv6 address.
std::string ToIPv6String(const ipv6_t& ip6);

// This class represents an ipv{4,6} address.
// It is thread-compatible but not thread-safe.
class IpAddr final {
 public:
  // Parses the `ip` address string and returns an `IpAddr` if successful.
  // Otherwise, returns `std::nullopt`.
  static std::optional<IpAddr> Create(std::string_view ip);

  // Default constructor.
  IpAddr() : ipaddr_() { DCHECK(IsIPv4()); }

  // Constructor for ipv4.
  IpAddr(const ipv4_t& ip4) : ipaddr_(ip4) {}

  // Constructor for ipv6.
  IpAddr(const ipv6_t& ip6) : ipaddr_(ip6) {}

  // Allows copy/move.
  ALLOW_COPY(IpAddr);
  ALLOW_MOVE(IpAddr);

  // Destructor.
  ~IpAddr() = default;

  // Returns the address family.
  int AddressFamily() const { return IsIPv4() ? AF_INET : AF_INET6; }

  // Returns true iff it is an ipv4 address.
  bool IsIPv4() const { return std::holds_alternative<ipv4_t>(ipaddr_); }

  // Returns true iff it is an ipv6 address.
  bool IsIPv6() const { return std::holds_alternative<ipv6_t>(ipaddr_); }

  // Returns the ipv4 address.
  const ipv4_t& IPv4Addr() const { return std::get<ipv4_t>(ipaddr_); }

  // Returns the ipv6 address.
  const ipv6_t& IPv6Addr() const { return std::get<ipv6_t>(ipaddr_); }

  // Equality operator.
  friend bool operator==(const IpAddr& a, const IpAddr& b);

  // Returns a hash signature of the ip address.
  size_t Hash() const {
    static_assert(assumptions::kAbslHashIsStableOnlyInOneProcessInvocation);
    return Hash(*this);
  }

  // Returns a hash signature of the ip address.
  static size_t Hash(const IpAddr& ip) {
    static_assert(assumptions::kAbslHashIsStableOnlyInOneProcessInvocation);
    return absl::Hash<IpAddr>{}(ip);
  }

  // Returns a string representation of the ip address.
  std::string ToString() const;

 private:
  // Calculates a hash value for the ip address.
  template <typename H>
  friend H AbslHashValue(H h, const IpAddr& ip) {
    static_assert(assumptions::kAbslHashIsStableOnlyInOneProcessInvocation);
    if (ip.IsIPv4()) {
      return H::combine(std::move(h), ip.IPv4Addr().s_addr);
    } else {
      DCHECK(ip.IsIPv6());
      return H::combine(std::move(h), absl::MakeConstSpan(ip.IPv6Addr().s6_addr,
                                                          sizeof(ipv6_t)));
    }
  }

 private:
  std::variant<ipv4_t, ipv6_t> ipaddr_;
};

inline std::ostream& operator<<(std::ostream& os, const IpAddr& ip) {
  return os << ip.ToString();
}

}  // namespace peregrine::internal

#endif  // PEREGRINE_SRC_INTERNAL_BASE_IPADDR_H_
