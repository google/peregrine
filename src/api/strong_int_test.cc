#include "src/api/strong_int.h"

#include <cstdint>
#include <type_traits>

#include "gtest/gtest.h"
#include "absl/container/flat_hash_set.h"
#include "absl/log/log.h"

namespace peregrine::internal::testing {
namespace {

DEFINE_STRONG_INT_TYPE(Id, int16_t);
DEFINE_STRONG_INT_TYPE(Fd, int16_t);

TEST(StrongIntTest, Constructor) {
  const Id x;
  EXPECT_EQ(x.value(), 0);
  LOG(INFO) << "x: " << x;

  const Id y(1);
  EXPECT_EQ(y.value(), 1);
  LOG(INFO) << "y: " << y;

  const auto z = y;
  EXPECT_EQ(z.value(), 1);
  LOG(INFO) << "z: " << z;

  const auto w(y);
  EXPECT_EQ(w.value(), 1);
  LOG(INFO) << "w: " << w;

  const Fd fd(1);
  EXPECT_EQ(fd.value(), y.value());
  LOG(INFO) << "fd: " << fd.ToString();
  LOG(INFO) << "fd: " << Fd(30'000);
  LOG(INFO) << "fd: " << Fd(-30'000);
}

TEST(StrongIntTest, EqualityAndHash) {
  const Id x(17);
  const Id y(17);
  const Id z(99);

  EXPECT_EQ(x, y);
  EXPECT_NE(x, z);
  EXPECT_EQ(x.Hash(), y.Hash());
  EXPECT_EQ(Id::Hash(x), Id::Hash(y));

  absl::flat_hash_set<Id> set;
  set.insert(x);
  EXPECT_EQ(set.size(), 1);
  EXPECT_TRUE(set.contains(x));
  EXPECT_FALSE(set.contains(z));

  set.insert(y);
  EXPECT_EQ(set.size(), 1);

  set.insert(z);
  EXPECT_EQ(set.size(), 2);
  EXPECT_TRUE(set.contains(z));
  EXPECT_FALSE(set.contains(Id(18)));

  set.erase(Id(99));
  EXPECT_EQ(set.size(), 1);
  EXPECT_TRUE(set.contains(x));
  EXPECT_FALSE(set.contains(z));

  set.erase(Id(17));
  EXPECT_TRUE(set.empty());
}

TEST(StrongIntTest, Range) {
  static_assert(std::is_same_v<Id::ValueType, int16_t>);
  EXPECT_EQ(Id::max(), Id(32767));
  EXPECT_EQ(Id::min(), Id(-32768));
}

}  // namespace
}  // namespace peregrine::internal::testing
