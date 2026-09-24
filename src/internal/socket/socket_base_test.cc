#include <memory>
#include <string>
#include <type_traits>
#include <utility>

#include "gtest/gtest.h"
#include "absl/log/check.h"
#include "src/internal/base/types.h"
#include "src/internal/socket/socket_tcp.h"
#include "src/internal/socket/socket_udp.h"
#include "src/internal/util/test_param.h"

namespace peregrine::internal::testing {
namespace {

using ::testing::Combine;
using ::testing::TestParamInfo;
using ::testing::TestWithParam;
using ::testing::Values;

std::string ToString(const TestParamInfo<SocketTestParam>& info) {
  return testing::ToString(info.param);
}

class SocketTest : public TestWithParam<SocketTestParam> {
 protected:
  SocketTest() : cfg_(GetParam()) {}

  template <typename T>
  static void Check(std::unique_ptr<T> s) {
    static_assert(std::is_same_v<T, TcpSocket> || std::is_same_v<T, UdpSocket>);
    static_assert(!std::is_copy_constructible_v<T>);
    static_assert(!std::is_copy_assignable_v<T>);
    static_assert(!std::is_move_constructible_v<T>);
    static_assert(!std::is_move_assignable_v<T>);

    CHECK_NE(s, nullptr);
    const fd_t fd = s->fd();
    CHECK_GE(fd.value(), 0);

    auto s2 = std::move(s);
    EXPECT_EQ(s, nullptr);
    EXPECT_NE(s2, nullptr);
    EXPECT_EQ(s2->fd(), fd);
  }

 protected:
  const SocketTestConfig cfg_;
};

INSTANTIATE_TEST_SUITE_P(, SocketTest,
                         Combine(/*family=*/Values(AF_INET, AF_INET6),
                                 /*blocking=*/Values(true, false)),
                         ToString);

TEST_P(SocketTest, TcpCopyMove) {
  std::unique_ptr<TcpSocket> s = TcpSocket::Create(cfg_.family, cfg_.blocking);
  Check(std::move(s));
}

TEST_P(SocketTest, UdpCopyMove) {
  std::unique_ptr<UdpSocket> s = UdpSocket::Create(cfg_.family, cfg_.blocking);
  Check(std::move(s));
}

}  // namespace
}  // namespace peregrine::internal::testing
