#ifndef PEREGRINE_SRC_INTERNAL_BASE_ENDPOINT_H_
#define PEREGRINE_SRC_INTERNAL_BASE_ENDPOINT_H_

#include <ostream>
#include <string>

#include "absl/hash/hash.h"
#include "absl/status/statusor.h"
#include "absl/strings/string_view.h"
#include "src/internal/assumptions.h"
#include "src/internal/base/types.h"
#include "src/internal/socket/ip_util.h"

namespace peregrine {

// This class represents a network endpoint, which is a combination of
// an IPv{4,6} address and a port number. All fields are immutable.
//
// `Endpoint` is used to uniquely identifies a process, whose control channel
// listens on it.
//
// It is thread-safe.
class Endpoint final {
 public:
  // Creates from a string, eg. "127.0.0.1:12345" or "[::1]:12345".
  static absl::StatusOr<Endpoint> Create(absl::string_view ipaddr_port);

  // Constructs from an IP address and a port number.
  Endpoint(absl::string_view ipaddr, port_t port)
      : ipaddr_(ipaddr), port_(port) {}

  // Allows copy constructor and copy assignment.
  Endpoint(const Endpoint& e) = default;
  Endpoint& operator=(const Endpoint& e) = default;

  // Allows move constructor and move assignment.
  Endpoint(Endpoint&& e) = default;
  Endpoint& operator=(Endpoint&& e) = default;

  // Destructor.
  ~Endpoint() = default;

  // Returns the IP address of the endpoint.
  ipaddr_t IpAddr() const { return ipaddr_; };

  // Returns the port of the endpoint.
  port_t Port() const { return port_; };

  // Returns true iff the endpoint is valid.
  bool IsValid() const {
    return (1 <= port_ && port_ <= 65535) &&
           (IsIPv4Addr(ipaddr_) || IsIPv6Addr(ipaddr_));
  }

  // Equality operator.
  friend constexpr bool operator==(const Endpoint& a, const Endpoint& b) {
    return a.ipaddr_ == b.ipaddr_ && a.port_ == b.port_;
  }

  // Returns the hash signature of the endpoint.
  HashValue Hash() const { return Hash(*this); }

  // Returns the hash signature of the endpoint.
  static HashValue Hash(const Endpoint& e) { return absl::Hash<Endpoint>{}(e); }

  // Returns the hash value of the endpoint.
  template <typename H>
  friend H AbslHashValue(H h, const Endpoint& e) {
    static_assert(assumptions::kAbslHashIsStableOnlyInOneProcessInvocation);
    return H::combine(std::move(h), e.ipaddr_, e.port_);
  }

  // Returns a string representation of the endpoint.
  std::string ToString() const;

 private:
  const std::string ipaddr_;
  const port_t port_;
};

inline std::ostream& operator<<(std::ostream& os, const Endpoint& e) {
  return os << e.ToString();
}

}  // namespace peregrine

#endif  // PEREGRINE_SRC_INTERNAL_BASE_ENDPOINT_H_
