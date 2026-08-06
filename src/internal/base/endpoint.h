#ifndef PEREGRINE_SRC_INTERNAL_BASE_ENDPOINT_H_
#define PEREGRINE_SRC_INTERNAL_BASE_ENDPOINT_H_

#include <cstddef>
#include <ostream>
#include <string>
#include <string_view>

#include "absl/hash/hash.h"
#include "absl/log/check.h"
#include "src/internal/assumptions.h"
#include "src/internal/base/ipaddr.h"
#include "src/internal/base/types.h"
#include "src/util/macro.h"

namespace peregrine::internal {

// This class represents a network endpoint, which is a combination of
// an ipv{4,6} address and a port number. It is used to uniquely identify
// a process, whose control channel listens on the `ip:port`.
// It is thread-compatible and but not thread-safe.
class Endpoint final {
 public:
  // Parses and creates an endpoint from a string, eg. "127.0.0.1:56789" or
  // "[::1]:56789". Returns an invalid endpoint if the string parsing fails.
  static Endpoint Create(std::string_view ipaddr_port);

  // Default constructor creates an invalid endpoint.
  Endpoint() : ipaddr_(), port_(0) {
    DCHECK(HasZeroIpAddr());
    DCHECK(!IsValid());
  }

  // Constructor for ipv4.
  Endpoint(ipv4_t ip4, port_t port) : ipaddr_(ip4), port_(port) {}

  // Constructor for ipv6.
  Endpoint(ipv6_t ip6, port_t port) : ipaddr_(ip6), port_(port) {}

  // Constructor for ipv{4,6}.
  Endpoint(const IpAddr& ip, port_t port) : ipaddr_(ip), port_(port) {}

  // Allows copy/move.
  ALLOW_COPY(Endpoint);
  ALLOW_MOVE(Endpoint);

  // Destructor.
  ~Endpoint() = default;

  // Returns true iff the endpoint ip address is zero ("0.0.0.0" or "::").
  bool HasZeroIpAddr() const { return ipaddr_.IsZero(); }

  // Returns true iff the endpoint is valid.
  bool IsValid() const {
    DCHECK(0 <= port_ && port_ <= 65535);
    return 1 <= port_;  // [1, 65535]
  }

  // Returns the ip address of the endpoint.
  const IpAddr& GetIpAddr() const { return ipaddr_; };

  // Returns true iff the endpoint has an ipv4 address.
  bool IsIPv4() const { return ipaddr_.IsIPv4(); }

  // Returns true iff the endpoint has an ipv6 address.
  bool IsIPv6() const { return ipaddr_.IsIPv6(); }

  // Returns the ipv4 address of the ipv4 endpoint.
  const ipv4_t& IPv4Addr() const {
    DCHECK(IsIPv4());
    return ipaddr_.IPv4Addr();
  }

  // Returns the ipv6 address of the ipv6 endpoint.
  const ipv6_t& IPv6Addr() const {
    DCHECK(IsIPv6());
    return ipaddr_.IPv6Addr();
  }

  // Returns the port of the endpoint.
  port_t Port() const { return port_; };

  // Builds a `sockaddr_in` struct for the ipv4 endpoint.
  struct sockaddr_in BuildIPv4Sockaddr() const;

  // Builds a `sockaddr_in6` struct for the ipv6 endpoint.
  struct sockaddr_in6 BuildIPv6Sockaddr() const;

  // Equality operator.
  friend bool operator==(const Endpoint& a, const Endpoint& b) {
    return a.port_ == b.port_ && a.ipaddr_ == b.ipaddr_;
  }

  // Returns a hash signature of the endpoint.
  size_t Hash() const {
    static_assert(assumptions::kAbslHashIsStableOnlyInOneProcessInvocation);
    return Hash(*this);
  }

  // Returns a hash signature of the endpoint.
  static size_t Hash(const Endpoint& e) {
    static_assert(assumptions::kAbslHashIsStableOnlyInOneProcessInvocation);
    return absl::Hash<Endpoint>{}(e);
  }

  // Returns a string representation of the endpoint.
  std::string ToString() const;

 private:
  // Calculates a hash value for the endpoint.
  template <typename H>
  friend H AbslHashValue(H h, const Endpoint& e) {
    static_assert(assumptions::kAbslHashIsStableOnlyInOneProcessInvocation);
    return H::combine(std::move(h), e.ipaddr_, e.port_);
  }

 private:
  IpAddr ipaddr_;
  port_t port_;
};

inline std::ostream& operator<<(std::ostream& os, const Endpoint& e) {
  return os << e.ToString();
}

}  // namespace peregrine::internal

#endif  // PEREGRINE_SRC_INTERNAL_BASE_ENDPOINT_H_
