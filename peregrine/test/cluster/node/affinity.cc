#include "peregrine/test/cluster/node/affinity.h"

#include <sched.h>

#include <algorithm>
#include <cerrno>
#include <cstring>
#include <fstream>
#include <iterator>
#include <sstream>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/strings/ascii.h"
#include "absl/strings/match.h"
#include "absl/strings/numbers.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/str_format.h"
#include "absl/strings/str_split.h"
#include "absl/strings/strip.h"

namespace peregrine::cluster {
namespace {

constexpr std::string_view kNumaPrefix = "numa:";

bool IsAllDigits(std::string_view s) {
  if (s.empty()) return false;
  for (char c : s) {
    if (!absl::ascii_isdigit(static_cast<unsigned char>(c))) {
      return false;
    }
  }
  return true;
}

absl::StatusOr<int> ParseSingleCpuId(std::string_view token,
                                     std::string_view full_list) {
  token = absl::StripAsciiWhitespace(token);
  int cpu = -1;
  if (!IsAllDigits(token) || !absl::SimpleAtoi(token, &cpu) || cpu < 0 ||
      cpu >= CPU_SETSIZE) {
    return absl::InvalidArgumentError(
        absl::StrCat("Invalid CPU ID '", token, "' in CPU list '", full_list,
                     "' (expected integer in [0, ", CPU_SETSIZE - 1, "])"));
  }
  return cpu;
}

absl::StatusOr<int> ParseNumaNodeId(std::string_view spec) {
  std::string_view node_str =
      absl::StripAsciiWhitespace(spec.substr(kNumaPrefix.size()));
  int node_id = -1;
  if (!IsAllDigits(node_str) || !absl::SimpleAtoi(node_str, &node_id) ||
      node_id < 0) {
    return absl::InvalidArgumentError(absl::StrCat(
        "Invalid NUMA affinity spec '", spec, "': expected 'numa:<node_id>'"));
  }
  return node_id;
}

absl::StatusOr<std::string> ReadSysfsFile(const std::string& path) {
  std::ifstream file(path);
  if (!file.is_open()) {
    return absl::NotFoundError(
        absl::StrCat("Failed to open sysfs CPU list file '", path, "'"));
  }
  std::ostringstream buffer;
  buffer << file.rdbuf();
  if (file.bad()) {
    return absl::InternalError(
        absl::StrCat("Failed to read sysfs CPU list file '", path, "'"));
  }
  return std::string(absl::StripAsciiWhitespace(buffer.str()));
}

}  // namespace

absl::StatusOr<std::vector<int>> ParseCpuList(std::string_view cpu_list) {
  std::string_view stripped = absl::StripAsciiWhitespace(cpu_list);
  if (stripped.empty()) {
    return absl::InvalidArgumentError("CPU list must not be empty");
  }

  std::vector<int> cpus;
  for (std::string_view part : absl::StrSplit(stripped, ',')) {
    part = absl::StripAsciiWhitespace(part);
    if (part.empty()) {
      return absl::InvalidArgumentError(
          absl::StrCat("Empty segment in CPU list '", cpu_list, "'"));
    }

    const size_t dash_pos = part.find('-');
    if (dash_pos == std::string_view::npos) {
      auto cpu_or = ParseSingleCpuId(part, cpu_list);
      if (!cpu_or.ok()) return cpu_or.status();
      cpus.push_back(*cpu_or);
      continue;
    }

    const std::string_view lhs = part.substr(0, dash_pos);
    const std::string_view rhs = part.substr(dash_pos + 1);
    if (absl::StrContains(rhs, '-')) {
      return absl::InvalidArgumentError(absl::StrCat(
          "Invalid CPU range '", part, "' in CPU list '", cpu_list, "'"));
    }
    auto start_or = ParseSingleCpuId(lhs, cpu_list);
    if (!start_or.ok()) return start_or.status();
    auto end_or = ParseSingleCpuId(rhs, cpu_list);
    if (!end_or.ok()) return end_or.status();
    if (*start_or > *end_or) {
      return absl::InvalidArgumentError(absl::StrCat(
          "Descending CPU range '", part, "' in CPU list '", cpu_list, "'"));
    }
    for (int c = *start_or; c <= *end_or; ++c) {
      cpus.push_back(c);
    }
  }

  std::sort(cpus.begin(), cpus.end());
  cpus.erase(std::unique(cpus.begin(), cpus.end()), cpus.end());
  return cpus;
}

absl::StatusOr<std::string> ValidateCpuAffinitySpec(std::string_view spec) {
  spec = absl::StripAsciiWhitespace(spec);
  if (spec.empty() || absl::EqualsIgnoreCase(spec, "none")) {
    return "";
  }
  if (absl::StartsWithIgnoreCase(spec, kNumaPrefix)) {
    auto node_id_or = ParseNumaNodeId(spec);
    if (!node_id_or.ok()) return node_id_or.status();
    return absl::StrFormat("numa:%d", *node_id_or);
  }
  auto parsed_or = ParseCpuList(spec);
  if (!parsed_or.ok()) return parsed_or.status();
  return std::string(spec);
}

absl::StatusOr<std::vector<int>> ResolveCpuAffinitySpec(
    std::string_view spec, std::string_view sysfs_root) {
  spec = absl::StripAsciiWhitespace(spec);
  if (spec.empty() || absl::EqualsIgnoreCase(spec, "none")) {
    return std::vector<int>{};
  }
  if (absl::StartsWithIgnoreCase(spec, kNumaPrefix)) {
    auto node_id_or = ParseNumaNodeId(spec);
    if (!node_id_or.ok()) return node_id_or.status();
    const std::string root(
        absl::StripSuffix(absl::StripAsciiWhitespace(sysfs_root), "/"));
    const std::string cpulist_path = absl::StrFormat(
        "%s/devices/system/node/node%d/cpulist", root, *node_id_or);
    auto raw_or = ReadSysfsFile(cpulist_path);
    if (!raw_or.ok()) return raw_or.status();
    return ParseCpuList(*raw_or);
  }
  return ParseCpuList(spec);
}

absl::StatusOr<std::vector<int>> GetCurrentCpuAffinity() {
  cpu_set_t mask;
  CPU_ZERO(&mask);
  if (sched_getaffinity(0, sizeof(cpu_set_t), &mask) != 0) {
    return absl::InternalError(
        absl::StrCat("sched_getaffinity failed: ", std::strerror(errno)));
  }
  std::vector<int> cpus;
  for (int c = 0; c < CPU_SETSIZE; ++c) {
    if (CPU_ISSET(c, &mask)) {
      cpus.push_back(c);
    }
  }
  return cpus;
}

absl::Status ApplyCpuAffinity(std::string_view spec,
                              std::string_view sysfs_root) {
  auto cpus_or = ResolveCpuAffinitySpec(spec, sysfs_root);
  if (!cpus_or.ok()) {
    return cpus_or.status();
  }
  std::vector<int> target_cpus = *std::move(cpus_or);
  if (target_cpus.empty()) {
    return absl::OkStatus();
  }

  const std::string_view trimmed_spec = absl::StripAsciiWhitespace(spec);
  if (absl::StartsWithIgnoreCase(trimmed_spec, kNumaPrefix)) {
    auto allowed_or = GetCurrentCpuAffinity();
    if (!allowed_or.ok()) {
      return allowed_or.status();
    }
    std::vector<int> intersected;
    std::set_intersection(target_cpus.begin(), target_cpus.end(),
                          allowed_or->begin(), allowed_or->end(),
                          std::back_inserter(intersected));
    if (intersected.empty()) {
      return absl::InvalidArgumentError(
          absl::StrCat("No CPUs in '", trimmed_spec,
                       "' are allowed by the current CPU affinity mask"));
    }
    target_cpus = std::move(intersected);
  }

  cpu_set_t mask;
  CPU_ZERO(&mask);
  for (int cpu : target_cpus) {
    CPU_SET(cpu, &mask);
  }
  if (sched_setaffinity(0, sizeof(cpu_set_t), &mask) != 0) {
    return absl::InternalError(absl::StrCat("sched_setaffinity failed for '",
                                            trimmed_spec,
                                            "': ", std::strerror(errno)));
  }
  return absl::OkStatus();
}

}  // namespace peregrine::cluster
