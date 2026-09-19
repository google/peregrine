#include "src/internal/base/hostinfo.h"

#include <algorithm>
#include <cstddef>
#include <string>
#include <string_view>
#include <vector>

#include "absl/container/flat_hash_set.h"
#include "absl/log/check.h"
#include "absl/strings/ascii.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/str_split.h"
#include "absl/strings/string_view.h"
#include "src/internal/base/endpoint.h"
#include "src/internal/base/nicinfo.h"

namespace peregrine::internal {

bool HostInfo::IsValid() const {
  const bool control_plane_valid = control_plane_listener.HasNonzeroIpPort();

  const bool data_plane_valid =
      !data_plane_listeners.empty() &&
      std::all_of(data_plane_listeners.begin(), data_plane_listeners.end(),
                  [](const NicInfo& nic) { return nic.IsValid(); });

  size_t num_endpoints = 1;
  absl::flat_hash_set<Endpoint> es = {control_plane_listener};
  for (const auto& nic : data_plane_listeners) {
    num_endpoints += nic.endpoints.size();
    es.insert(nic.endpoints.begin(), nic.endpoints.end());
  }
  const bool unique_endpoints = es.size() == num_endpoints;

  absl::flat_hash_set<std::string> nic_names;
  for (const auto& nic : data_plane_listeners) {
    nic_names.insert(nic.name);
  }
  const bool unique_nic_names = nic_names.size() == data_plane_listeners.size();

  return control_plane_valid && data_plane_valid && unique_endpoints &&
         unique_nic_names;
}

HostInfo HostInfo::Create(std::string_view s) {
  const HostInfo invalid;
  DCHECK(!invalid.IsValid());

  // "10.0.0.1:10000;
  //   lo/ip/127.0.0.1:35247,[::1]:35247;
  //   eth0/ip/10.0.0.1:43521,10.0.0.2:51691;
  //   irdma0/rdma/[2202:a05:7901:1000::]:1"
  HostInfo host;
  int i = 0;
  const auto parts = absl::StrSplit(s, ';', absl::SkipEmpty());
  for (const auto p : parts) {
    const std::string_view ss = absl::StripAsciiWhitespace(p);
    if (i++ == 0) {
      host.control_plane_listener = Endpoint::Create(ss);
    } else {
      const NicInfo nic = NicInfo::Create(ss);
      if (!nic.IsValid()) return invalid;
      host.data_plane_listeners.push_back(nic);
    }
  }
  return host.IsValid() ? host : invalid;
}

std::string HostInfo::ToString() const {
  std::string s = control_plane_listener.ToString();
  for (const auto& nic : data_plane_listeners) {
    absl::StrAppend(&s, "; ", nic.ToString());
  }
  return s;
}

}  // namespace peregrine::internal
