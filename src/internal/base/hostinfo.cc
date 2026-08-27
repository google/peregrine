#include "src/internal/base/hostinfo.h"

#include <algorithm>
#include <string>
#include <string_view>

#include "absl/container/flat_hash_set.h"
#include "absl/log/check.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/str_split.h"
#include "src/internal/base/endpoint.h"

namespace peregrine::internal {

bool HostInfo::IsValid() const {
  const bool control_plane_valid = control_plane_listener.HasNonzeroIpPort();

  const bool data_plane_not_empty = !data_plane_listeners.empty();
  const bool data_plane_valid =
      std::all_of(data_plane_listeners.begin(), data_plane_listeners.end(),
                  [](const Endpoint& e) { return e.HasNonzeroIpPort(); });

  const bool rdma_interfaces_valid =
      std::all_of(rdma_interfaces.begin(), rdma_interfaces.end(),
                  [](const RdmaInterface& r) { return r.IsValid(); });

  absl::flat_hash_set<Endpoint> es = {control_plane_listener};
  es.insert(data_plane_listeners.begin(), data_plane_listeners.end());
  const bool unique_endpoints = es.size() == 1 + data_plane_listeners.size();

  absl::flat_hash_set<std::string_view> rdma_names;
  for (const auto& r : rdma_interfaces) rdma_names.insert(r.name);
  const bool unique_rdma_names = rdma_names.size() == rdma_interfaces.size();

  return control_plane_valid && data_plane_not_empty && data_plane_valid &&
         rdma_interfaces_valid && unique_endpoints && unique_rdma_names;
}

HostInfo HostInfo::Create(std::string_view ipaddr_port_pairs) {
  const HostInfo invalid;
  DCHECK(!invalid.IsValid());

  HostInfo host;
  int i = 0;
  const auto parts = absl::StrSplit(ipaddr_port_pairs, absl::ByAnyChar(",; \t"),
                                    absl::SkipEmpty());
  for (const auto ipaddr_port : parts) {
    const Endpoint e = Endpoint::Create(ipaddr_port);
    if (i++ == 0) {
      host.control_plane_listener = e;
    } else {
      host.data_plane_listeners.push_back(e);
    }
  }
  return host.IsValid() ? host : invalid;
}

std::string HostInfo::ToString() const {
  std::string s = absl::StrCat("host: ", control_plane_listener.ToString());
  for (const auto& e : data_plane_listeners) {
    absl::StrAppend(&s, ", ", e.ToString());
  }
  for (const auto& r : rdma_interfaces) {
    absl::StrAppend(&s, ", ", r.ToString());
  }
  return s;
}

}  // namespace peregrine::internal
