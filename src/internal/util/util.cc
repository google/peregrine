#include "src/internal/util/util.h"

#include <cstddef>
#include <numeric>
#include <sstream>
#include <string>
#include <thread>  // NOLINT

#include "src/internal/base/types.h"

namespace peregrine::internal {

std::string ThreadId() {
  std::stringstream ss;
  ss << std::this_thread::get_id();
  return ss.str();
}

size_t TotalLength(const IoVec* const iov, const int n) {
  return std::accumulate(
      iov, iov + n, size_t{0},
      [](size_t sum, const IoVec& v) { return sum + v.iov_len; });
}

}  // namespace peregrine::internal
