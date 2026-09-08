#include "src/internal/socket/psp/psp.h"

#include <string>

#include "gtest/gtest.h"
#include "absl/log/check.h"
#include "absl/log/log.h"
#include "absl/strings/string_view.h"

namespace peregrine::internal::psp::testing {
namespace {

const PspToken kInvalid(Spi(0));
const PspToken kValid(Spi(1));

TEST(PspTokenTest, PspToken) {
  EXPECT_TRUE(kValid.IsValid());
  EXPECT_EQ(kValid.spi.value(), 1);
  EXPECT_EQ(kValid.gen.value(), 0);
  EXPECT_EQ(kValid.key.size(), kPspKeyLen);
  EXPECT_EQ(kValid.key[0], 0);
  EXPECT_EQ(kValid.key[kPspKeyLen - 1], 0);
  LOG(INFO) << kValid;

  EXPECT_FALSE(kInvalid.IsValid());
  LOG(INFO) << kInvalid;
}

}  // namespace
}  // namespace peregrine::internal::psp::testing
