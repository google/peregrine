#include "src/internal/util/test_iov.h"

#include <cstddef>
#include <vector>

#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "absl/log/check.h"
#include "absl/log/log.h"
#include "absl/types/span.h"
#include "src/api/transport_types.h"
#include "src/internal/base/types.h"

namespace peregrine::internal::testing {
namespace {

using ::testing::ElementsAreArray;

class IoVecTest : public ::testing::Test {
 protected:
  IoVecTest()
      : bytes1_(kSize, 11),
        bytes2_(kSize, 22),
        iov1_(bytes1_.data(), bytes1_.size()),
        iov2_(bytes2_.data(), bytes2_.size()) {
    CHECK_NE(bytes1_, bytes2_);
  }

 protected:
  static constexpr size_t kSize = 8;
  std::vector<Byte> bytes1_;
  std::vector<Byte> bytes2_;
  const IoVec iov1_;
  const IoVec iov2_;
};

TEST_F(IoVecTest, Linearize) {
  const std::vector<IoVec> iovecs = {iov1_, iov2_};
  const OwnedIoVec owned = TestOnly_Linearize(iovecs);
  EXPECT_NE(owned.data, nullptr);
  EXPECT_EQ(owned.size, 2 * kSize);

  const absl::Span<const Byte> owned1(owned.data.get(), kSize);
  const absl::Span<const Byte> owned2(owned.data.get() + kSize, kSize);
  EXPECT_THAT(owned1, ElementsAreArray(bytes1_));
  EXPECT_THAT(owned2, ElementsAreArray(bytes2_));
}

}  // namespace
}  // namespace peregrine::internal::testing
