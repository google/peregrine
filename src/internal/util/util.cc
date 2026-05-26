#include "src/internal/util/util.h"

#include <cstddef>
#include <numeric>
#include <sstream>
#include <string>
#include <thread>  // NOLINT

namespace peregrine {

std::string ThreadId() {
  std::stringstream ss;
  ss << std::this_thread::get_id();
  return ss.str();
}

size_t TotalLength(const struct iovec* const iov, const int n) {
  return std::accumulate(
      iov, iov + n, size_t{0},
      [](size_t sum, const struct iovec& v) { return sum + v.iov_len; });
}

}  // namespace peregrine
