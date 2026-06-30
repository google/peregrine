#include "src/util/app.h"

#include <cstddef>

#include "gmock/gmock.h"
#include "gtest/gtest.h"

namespace peregrine::util::testing {
namespace {

using ::testing::Each;
using ::testing::Eq;
using ::testing::Ne;

TEST(AppTest, Basic) {
  constexpr size_t kSize = 1024;
  constexpr int kNumConnsPerPeer = 1;
  App app(kSize, kNumConnsPerPeer);

  EXPECT_NE(app.DataPtr(), nullptr);
  EXPECT_EQ(app.DataSize(), kSize);

  app.GenData();
  EXPECT_THAT(app.Data(), Each(Ne(0)));

  app.ClearData();
  EXPECT_THAT(app.Data(), Each(Eq(0)));
}

}  // namespace
}  // namespace peregrine::util::testing
