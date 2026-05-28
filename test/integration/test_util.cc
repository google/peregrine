#include "test/integration/test_util.h"

#include <algorithm>

#include "absl/log/check.h"
#include "absl/random/random.h"
#include "src/api/types.h"

namespace peregrine::integration_test {

void UserApplication::ClearData() { std::fill(data_.begin(), data_.end(), 0); }

void UserApplication::GenData() {
  absl::BitGen bitgen;
  for (int i = 0; i < data_.size(); ++i) {
    data_[i] = absl::Uniform<Byte>(absl::IntervalClosed, bitgen, 0x01, 0xff);
    DCHECK_NE(data_[i], 0);
  }
}

}  // namespace peregrine::integration_test
