#include "src/internal/base/nicinfo.h"

#include <algorithm>
#include <string>
#include <string_view>
#include <vector>

#include "absl/log/check.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/str_join.h"
#include "absl/strings/str_split.h"
#include "absl/strings/string_view.h"
#include "src/internal/assumptions.h"
#include "src/internal/base/endpoint.h"
#include "src/util/nic.h"

namespace peregrine::internal {

namespace {
using util::NicType::kInvalid;
using util::NicType::kIP;
using util::NicType::kRDMA;

constexpr absl::string_view kNicInfoSep = "/";
constexpr absl::string_view kEndpointSep = ",";

}  // namespace

bool NicInfo::IsValid() const {
  if (name.empty()) return false;
  if (type == kInvalid) return false;
  if (endpoints.empty()) return false;
  return std::all_of(
      endpoints.begin(), endpoints.end(), [this](const Endpoint& e) {
        static_assert(assumptions::kUseIpAddrToRepresentRdmaRoCEv2Gid);
        DCHECK(type == kIP || type == kRDMA);
        return e.HasNonzeroIpPort() && (type != kRDMA || e.IsIPv6());
      });
}

std::string NicInfo::ToString() const {
  // "lo/ip/127.0.0.1:35247,[::1]:35247"
  // "eth0/ip/183.10.20.11:43521,183.10.30.59:51691"
  // "irdma0/rdma/[2202:a05:7901:1000::]:1"
  return absl::StrCat(name, kNicInfoSep, util::ToString(type), kNicInfoSep,
                      absl::StrJoin(endpoints, kEndpointSep));
}

NicInfo NicInfo::Create(std::string_view s) {
  const NicInfo invalid;
  DCHECK(!invalid.IsValid());

  constexpr int kNumParts = 3;
  const std::vector<std::string_view> p = absl::StrSplit(
      s, absl::MaxSplits(kNicInfoSep, kNumParts - 1), absl::SkipEmpty());
  if (p.size() != kNumParts) return invalid;

  const std::string name = std::string(p[0]);
  const util::NicType type = util::FromString(p[1]);
  std::vector<Endpoint> endpoints;
  for (const auto e : absl::StrSplit(p[2], kEndpointSep, absl::SkipEmpty())) {
    endpoints.push_back(Endpoint::Create(e));
  }
  const NicInfo nic(name, type, endpoints);
  return nic.IsValid() ? nic : invalid;
}

}  // namespace peregrine::internal
