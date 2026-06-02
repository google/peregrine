#include "src/internal/util/util.h"

#include <algorithm>
#include <cstddef>
#include <numeric>
#include <sstream>
#include <string>
#include <thread>  // NOLINT

#include "absl/log/check.h"
#include "absl/types/span.h"
#include "src/internal/base/types.h"

namespace peregrine::internal {

bool IsValid(absl::Span<const IoVec> iovecs) {
  return std::all_of(iovecs.begin(), iovecs.end(),
                     [](const IoVec& v) { return IsValid(v); });
}

size_t TotalLength(const absl::Span<const IoVec> iovecs) {
  return std::accumulate(iovecs.begin(), iovecs.end(), size_t{0},
                         [](size_t sum, const IoVec& v) {
                           DCHECK(IsValid(v));
                           return sum + v.iov_len;
                         });
}

std::string ThreadId() {
  std::stringstream ss;
  ss << std::this_thread::get_id();
  return ss.str();
}

}  // namespace peregrine::internal
