#include "src/internal/lib/bitset.h"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <numeric>
#include <string>
#include <vector>

#include "absl/log/check.h"
#include "absl/strings/str_format.h"

namespace peregrine::internal {

uint32_t Bitset::Count() const {
  return std::accumulate(
      units_.begin(), units_.end(), 0,
      [](uint32_t sum, const Unit& u) { return sum + u.bits.count(); });
}

bool Bitset::IsEmpty() const {
  return std::all_of(units_.begin(), units_.end(),
                     [](const Unit& u) { return u.bits.none(); });
}

namespace {
std::string HexString(const uint64_t v) {
  std::string s = absl::StrFormat("%016x", v);
  s.insert(12, ",");
  s.insert(8, ",");
  s.insert(4, ",");
  return s;
}
}  // namespace

std::string Bitset::ToString() const {
  const size_t n = units_.size();
  DCHECK_GE(n, 1);
  const std::string prefix =
      absl::StrFormat("%d/%d/%d", Count(), size_, n * kUnitSize);

  const std::string first = HexString(units_.back().bits.to_ullong());
  if (n <= 1) {
    return absl::StrFormat("%s(0x%s)", prefix, first);
  } else {
    const std::string last = HexString(units_.front().bits.to_ullong());
    return absl::StrFormat("%s(0x%s%s%s)", prefix, first,
                           n <= 2 ? "," : ",...,", last);
  }
}

}  // namespace peregrine::internal
