#ifndef PEREGRINE_SRC_INTERNAL_BASE_NICINFO_H_
#define PEREGRINE_SRC_INTERNAL_BASE_NICINFO_H_

#include <ostream>
#include <string>
#include <string_view>
#include <vector>

#include "absl/log/check.h"
#include "absl/types/span.h"
#include "src/internal/assumptions.h"
#include "src/internal/base/endpoint.h"
#include "src/util/nic.h"

namespace peregrine::internal {

static_assert(assumptions::kUseIpAddrToRepresentRdmaRoCEv2Gid);
struct NicInfo {
  std::string name;  // "lo", "eth0", "irdma0"
  util::NicType type;
  std::vector<Endpoint> endpoints;

  // Default constructor.
  NicInfo() : name(""), type(util::NicType::kInvalid) { DCHECK(!IsValid()); }

  // Constructor.
  NicInfo(std::string_view name, util::NicType type,
          absl::Span<const Endpoint> es)
      : name(name), type(type), endpoints(es.begin(), es.end()) {}

  // Parses and creates nic info from a string.
  // Returns invalid nic info if the string parsing fails.
  static NicInfo Create(std::string_view s);

  // Returns true iff the nic info is valid.
  bool IsValid() const;

  // Returns a string representation.
  std::string ToString() const;

  bool operator==(const NicInfo& other) const = default;
};

inline std::ostream& operator<<(std::ostream& os, const NicInfo& nic) {
  return os << nic.ToString();
}

}  // namespace peregrine::internal

#endif  // PEREGRINE_SRC_INTERNAL_BASE_NICINFO_H_
