#include "test/benchmark/types.h"

#include <cstdint>
#include <string>

#include "absl/log/check.h"
#include "absl/numeric/bits.h"
#include "absl/strings/match.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/str_format.h"
#include "absl/strings/string_view.h"
#include "absl/time/time.h"

namespace peregrine::benchmark {

namespace {
constexpr int64_t kGiB = 1LL << 30;
constexpr int64_t kMiB = 1LL << 20;
constexpr int64_t kKiB = 1LL << 10;
constexpr float kGiBf = static_cast<float>(kGiB);
constexpr float kMiBf = static_cast<float>(kMiB);
constexpr float kKiBf = static_cast<float>(kKiB);
}  // namespace

std::string ToString(const Mbps mbps) {
  if (const auto v = mbps.value(); v >= 1000) {
    return v % 1000 == 0 ? absl::StrFormat("%dGbps", v / 1000)
                         : absl::StrFormat("%.2fGbps", ToGbps(mbps));
  } else {  // NOLINT
    return absl::StrFormat("%dMbps", v);
  }
}

std::string ToString(const uint64_t bytes) {
  if (absl::has_single_bit(static_cast<uint64_t>(bytes))) {
    if (bytes >= kGiB) {
      return absl::StrFormat("%dGiB", bytes >> 30);
    } else if (bytes >= kMiB) {  // NOLINT
      return absl::StrFormat("%dMiB", bytes >> 20);
    } else if (bytes >= kKiB) {
      return absl::StrFormat("%dKiB", bytes >> 10);
    } else {
      return absl::StrFormat("%dB", bytes);
    }
  } else {
    if (bytes >= kGiB) {
      return absl::StrFormat("%.2fGiB", static_cast<float>(bytes) / kGiBf);
    } else if (bytes >= kMiB) {  // NOLINT
      return absl::StrFormat("%.2fMiB", static_cast<float>(bytes) / kMiBf);
    } else if (bytes >= kKiB) {
      return absl::StrFormat("%.2fKiB", static_cast<float>(bytes) / kKiBf);
    } else {
      return absl::StrFormat("%dB", bytes);
    }
  }
}

Mbps CalcRate(uint64_t bytes, absl::Duration interval) {
  DCHECK_GE(bytes, 0);
  DCHECK_GE(interval, absl::Microseconds(1));
  const uint64_t bits = bytes * 8;
  const int64_t us = absl::ToInt64Microseconds(interval);
  DCHECK_GE(us, 1);
  return Mbps((bits + us / 2) / us);
}

std::string ToString(const WorkloadType workload) {
  switch (workload) {
    case WorkloadType::kSerialFixedWrite:
      return "serial_fixed_write";
  }
  return absl::StrFormat("Unknown(%d)", static_cast<int>(workload));
}

bool AbslParseFlag(absl::string_view text, WorkloadType* workload,
                   std::string* error) {
  if (absl::EqualsIgnoreCase(text, "serial_fixed_write")) {
    *workload = WorkloadType::kSerialFixedWrite;
    return true;
  }
  *error = absl::StrCat("unknown workload '", text,
                        "'. Supported workloads: [serial_fixed_write]");
  return false;
}

std::string AbslUnparseFlag(WorkloadType workload) {
  return ToString(workload);
}

}  // namespace peregrine::benchmark
