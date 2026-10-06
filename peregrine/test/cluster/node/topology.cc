#include "peregrine/test/cluster/node/topology.h"

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/strings/ascii.h"
#include "absl/strings/match.h"
#include "absl/strings/numbers.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/str_format.h"
#include "absl/strings/str_join.h"
#include "absl/strings/str_split.h"
#include "absl/types/span.h"
#include "peregrine/test/cluster/node/config.h"

namespace peregrine::cluster {

absl::StatusOr<std::vector<PeerTarget>> ParseTargets(std::string_view raw) {
  std::string_view trimmed = absl::StripAsciiWhitespace(raw);
  if (trimmed.empty()) {
    return std::vector<PeerTarget>{};
  }

  std::vector<PeerTarget> targets;
  for (std::string_view token :
       absl::StrSplit(trimmed, ',', absl::SkipEmpty())) {
    token = absl::StripAsciiWhitespace(token);
    if (token.empty()) continue;

    const size_t at_pos = token.rfind('@');
    if (at_pos == std::string_view::npos || at_pos == 0 ||
        at_pos + 1 >= token.size()) {
      return absl::InvalidArgumentError(absl::StrCat(
          "Invalid target '", token, "': expected '<ip>:<port>@<raddr>'"));
    }

    std::string_view endpoint = token.substr(0, at_pos);
    std::string_view raddr_str = token.substr(at_pos + 1);
    if (!absl::StrContains(endpoint, ':')) {
      return absl::InvalidArgumentError(absl::StrCat(
          "Invalid target endpoint '", endpoint, "': missing ':<port>'"));
    }

    uint64_t raddr = 0;
    if (!absl::SimpleAtoi(raddr_str, &raddr) || raddr == 0) {
      return absl::InvalidArgumentError(
          absl::StrCat("Invalid target raddr '", raddr_str, "' in '", token,
                       "': expected positive uint64"));
    }

    targets.push_back(PeerTarget{
        .endpoint = std::string(endpoint),
        .raddr = raddr,
    });
  }

  return targets;
}

std::string FormatTargets(absl::Span<const PeerTarget> targets) {
  return absl::StrJoin(targets, ",", [](std::string* out, const PeerTarget& t) {
    absl::StrAppendFormat(out, "%s@%u", t.endpoint, t.raddr);
  });
}

absl::StatusOr<TrafficPattern> ParseTrafficPattern(std::string_view raw) {
  std::string_view s = absl::StripAsciiWhitespace(raw);
  if (absl::EqualsIgnoreCase(s, "all_to_all")) {
    return TrafficPattern::kAllToAll;
  }
  if (absl::EqualsIgnoreCase(s, "round_robin")) {
    return TrafficPattern::kRoundRobin;
  }
  return absl::InvalidArgumentError(
      absl::StrCat("Invalid traffic_pattern '", raw,
                   "': expected 'all_to_all' or 'round_robin'"));
}

std::string_view TrafficPatternToString(TrafficPattern pattern) {
  switch (pattern) {
    case TrafficPattern::kAllToAll:
      return "all_to_all";
    case TrafficPattern::kRoundRobin:
      return "round_robin";
  }
  return "unknown";
}

std::vector<std::vector<PeerTarget>> BuildTargetAssignments(
    int node_index, int num_local_instances,
    absl::Span<const PeerTarget> all_targets, TrafficPattern pattern,
    bool exclude_self, absl::Span<const std::string> local_endpoints) {
  if (num_local_instances <= 0) {
    return {};
  }
  std::vector<std::vector<PeerTarget>> assignments(num_local_instances);
  if (all_targets.empty()) {
    return assignments;
  }

  for (int i = 0; i < num_local_instances; ++i) {
    const std::string_view self_ep =
        (exclude_self && static_cast<size_t>(i) < local_endpoints.size())
            ? std::string_view(local_endpoints[i])
            : std::string_view();

    switch (pattern) {
      case TrafficPattern::kAllToAll: {
        assignments[i].reserve(all_targets.size());
        for (const PeerTarget& target : all_targets) {
          if (exclude_self && !self_ep.empty() && target.endpoint == self_ep) {
            continue;
          }
          assignments[i].push_back(target);
        }
        break;
      }
      case TrafficPattern::kRoundRobin: {
        const size_t global_idx = static_cast<size_t>(node_index) *
                                      static_cast<size_t>(num_local_instances) +
                                  static_cast<size_t>(i);
        const PeerTarget& chosen = all_targets[global_idx % all_targets.size()];
        if (!(exclude_self && !self_ep.empty() && chosen.endpoint == self_ep)) {
          assignments[i].push_back(chosen);
        }
        break;
      }
    }
  }
  return assignments;
}

}  // namespace peregrine::cluster
