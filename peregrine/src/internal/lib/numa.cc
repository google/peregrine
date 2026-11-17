#include "peregrine/src/internal/lib/numa.h"

#include <pthread.h>
#include <sched.h>

#include <algorithm>
#include <cstddef>
#include <fstream>
#include <string>
#include <string_view>
#include <vector>

#include "absl/status/status.h"
#include "absl/strings/ascii.h"
#include "absl/strings/numbers.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/str_split.h"

namespace peregrine::internal {

std::vector<int> GetNumaCpus(const int numa_node) {
  if (numa_node < 0) return {};

  // Filter sysfs node CPUs against the calling thread's allowed cpuset.
  cpu_set_t allowed;
  CPU_ZERO(&allowed);
  if (::sched_getaffinity(0, sizeof(allowed), &allowed) != 0) return {};

  std::ifstream file(
      absl::StrCat("/sys/devices/system/node/node", numa_node, "/cpulist"));
  std::string content;
  if (!std::getline(file, content)) return {};

  std::vector<int> cpus;
  for (std::string_view token :
       absl::StrSplit(content, ',', absl::SkipEmpty())) {
    token = absl::StripAsciiWhitespace(token);
    const size_t dash = token.find('-');
    int start = -1;
    int end = -1;
    if (dash == std::string_view::npos) {
      if (!absl::SimpleAtoi(token, &start)) continue;
      end = start;
    } else if (!absl::SimpleAtoi(token.substr(0, dash), &start) ||
               !absl::SimpleAtoi(token.substr(dash + 1), &end)) {
      continue;
    }
    for (int c = std::max(0, start); c <= std::min(end, CPU_SETSIZE - 1); ++c) {
      if (CPU_ISSET(c, &allowed)) cpus.push_back(c);
    }
  }
  return cpus;
}

absl::Status PinCurrentThreadToCpu(const int cpu_id) {
  if (cpu_id < 0) return absl::OkStatus();
  if (cpu_id >= CPU_SETSIZE) {
    return absl::InvalidArgumentError(
        absl::StrCat("cpu_id out of range: ", cpu_id));
  }
  cpu_set_t set;
  CPU_ZERO(&set);
  CPU_SET(cpu_id, &set);
  const int err = ::pthread_setaffinity_np(::pthread_self(), sizeof(set), &set);
  if (err != 0) {
    return absl::ErrnoToStatus(
        err, absl::StrCat("pthread_setaffinity_np failed for cpu ", cpu_id));
  }
  return absl::OkStatus();
}

}  // namespace peregrine::internal
