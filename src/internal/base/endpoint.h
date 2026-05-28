#ifndef PEREGRINE_SRC_INTERNAL_BASE_ENDPOINT_H_
#define PEREGRINE_SRC_INTERNAL_BASE_ENDPOINT_H_

#include <ostream>
#include <string>

#include "absl/hash/hash.h"
#include "absl/log/check.h"
#include "absl/status/statusor.h"
#include "absl/strings/string_view.h"
#include "absl/types/span.h"
#include "src/internal/assumptions.h"
#include "src/internal/base/types.h"

namespace peregrine {

// This class represents a network endpoint, which is a combination of
// an IPv{4,6} address and a port number. It is used to uniquely identifies
// a process, whose control channel listens on the `ip:port`.
//
// It is thread-compatible and but not thread-safe.
class Endpoint final {
 public:
  // Creates from a string, eg. "127.0.0.1:12345" or "[::1]:12345".
  static absl::StatusOr<Endpoint> Create(absl::string_view ipaddr_port);

  // Default constructor creates an invalid endpoint.
  Endpoint() : ipaddr_(), port_(0) { DCHECK(!IsValid()); }

  // Constructor for ipv4.
  Endpoint(ipv4_t ip4, port_t port) : ipaddr_(ip4), port_(port) {}

  // Constructor for ipv6.
  Endpoint(ipv6_t ip6, port_t port) : ipaddr_(ip6), port_(port) {}

  // Allows copy constructor and copy assignment.
  Endpoint(const Endpoint& e) = default;
  Endpoint& operator=(const Endpoint& e) = default;

  // Allows move constructor and move assignment.
  Endpoint(Endpoint&& e) = default;
  Endpoint& operator=(Endpoint&& e) = default;

  // Destructor.
  ~Endpoint() = default;

  // Returns the ip address of the endpoint.
  IpAddr GetIpAddr() const { return ipaddr_; };

  // Returns the port of the endpoint.
  port_t Port() const { return port_; };

  // Returns true iff the endpoint is valid.
  bool IsValid() const { return 1 <= port_ && port_ <= 65535; }

  // Equality operator.
  friend bool operator==(const Endpoint& a, const Endpoint& b);

  // Returns the hash signature of the endpoint.
  HashValue Hash() const {
    static_assert(assumptions::kAbslHashIsStableOnlyInOneProcessInvocation);
    return Hash(*this);
  }

  // Returns the hash signature of the endpoint.
  static HashValue Hash(const Endpoint& e) {
    static_assert(assumptions::kAbslHashIsStableOnlyInOneProcessInvocation);
    return absl::Hash<Endpoint>{}(e);
  }

  // Returns a string representation of the endpoint.
  std::string ToString() const;

 private:
  // Returns the hash value of the endpoint.
  template <typename H>
  friend H AbslHashValue(H h, const Endpoint& e) {
    static_assert(assumptions::kAbslHashIsStableOnlyInOneProcessInvocation);
    if (IsIPv4(e.ipaddr_)) {
      const ipv4_t& ip4 = std::get<ipv4_t>(e.ipaddr_);
      return H::combine(std::move(h), ip4.s_addr, e.port_);
    } else {
      const ipv6_t& ip6 = std::get<ipv6_t>(e.ipaddr_);
      const auto v = absl::MakeConstSpan(ip6.s6_addr, sizeof(ip6.s6_addr));
      return H::combine(std::move(h), v, e.port_);
    }
  }

 private:
  IpAddr ipaddr_;
  port_t port_;
};

inline std::ostream& operator<<(std::ostream& os, const Endpoint& e) {
  return os << e.ToString();
}

}  // namespace peregrine

#endif  // PEREGRINE_SRC_INTERNAL_BASE_ENDPOINT_H_
