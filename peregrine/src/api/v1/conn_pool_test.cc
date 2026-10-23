#include "peregrine/src/api/v1/conn_pool.h"

#include <arpa/inet.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <signal.h>
#include <sys/socket.h>
#include <unistd.h>

#include <memory>
#include <string>
#include <string_view>

#include "gtest/gtest.h"
#include "absl/log/check.h"
#include "absl/status/status_matchers.h"
#include "absl/time/clock.h"
#include "absl/time/time.h"
#include "peregrine/src/internal/base/endpoint.h"
#include "peregrine/src/internal/base/hostinfo.h"
#include "peregrine/src/internal/socket/tcp_manager.h"
#include "peregrine/src/internal/util/test_param.h"
#include "peregrine/src/internal/util/test_util.h"
#include "peregrine/src/util/thread.h"

namespace peregrine::v1::testing {
namespace {

using internal::Endpoint;
using internal::HostInfo;
using internal::TcpManager;
using internal::testing::SocketTestConfig;
using internal::testing::SocketTestParam;
using internal::testing::TestOnly_LocalHostInfo;
using ::testing::Combine;
using ::testing::TestParamInfo;
using ::testing::TestWithParam;
using ::testing::Values;

std::string ToString(const TestParamInfo<SocketTestParam>& info) {
  return internal::testing::ToString(info.param);
}

class ConnPoolTest : public TestWithParam<SocketTestParam> {
 protected:
  ConnPoolTest()
      : cfg_(GetParam()),
        self_(TestOnly_LocalHostInfo(cfg_.family, /*tcp=*/true)),
        mgr_(internal::TcpManager::Create(self_)) {
    const auto& ni = self_.data_plane_listeners.front();
    const auto v4 = ni.PickIPv4Localhost();
    const auto v6 = ni.PickIPv6Localhost();
    CHECK(v4.has_value());
    CHECK(v6.has_value());
    peer4_ = v4.value();
    peer6_ = v6.value();
    CHECK(self_.IsValid());
    CHECK(peer4_.HasNonzeroIpPort());
    CHECK(peer6_.HasNonzeroIpPort());
    CHECK_NE(mgr_, nullptr);
  }

  static void ShortSleep() { absl::SleepFor(absl::Milliseconds(100)); }

  ~ConnPoolTest() override { pool_.Close(); }

 protected:
  const SocketTestConfig cfg_;
  HostInfo self_;
  Endpoint peer4_;
  Endpoint peer6_;
  std::unique_ptr<internal::TcpManager> mgr_;
  ConnPool pool_;
};

INSTANTIATE_TEST_SUITE_P(, ConnPoolTest,
                         Combine(/*family=*/Values(AF_INET, AF_INET6),
                                 /*blocking=*/Values(true, false)),
                         ToString);

TEST_P(ConnPoolTest, MultiIpPoolingIsolation) {
  util::Thread ta([&]() { mgr_->Start(cfg_.blocking); });
  ShortSleep();

  constexpr std::string_view local_ipv4a = "127.0.0.1";
  constexpr std::string_view local_ipv4b = "127.0.0.2";
  const std::string peer = peer4_.ToString();

  // Borrow and return a connection under local_ipv4a.
  const auto fd1 = pool_.Borrow(peer, local_ipv4a);
  ABSL_ASSERT_OK(fd1) << fd1.status().message();
  pool_.Return(/*ok=*/true, *fd1, peer, local_ipv4a);

  // Borrow and return a connection under local_ipv4b.
  const auto fd2 = pool_.Borrow(peer, local_ipv4b);
  ABSL_ASSERT_OK(fd2) << fd2.status().message();
  EXPECT_NE(*fd1, *fd2);
  pool_.Return(/*ok=*/true, *fd2, peer, local_ipv4b);

  // Borrow and return a connection under local_ipv4a again.
  const auto fd3 = pool_.Borrow(peer, local_ipv4a);
  ABSL_ASSERT_OK(fd3) << fd3.status().message();
  EXPECT_EQ(*fd1, *fd3);
  pool_.Return(/*ok=*/true, *fd3, peer, local_ipv4a);

  // Borrow and return a connection under local_ipv4b again.
  const auto fd4 = pool_.Borrow(peer, local_ipv4b);
  ABSL_ASSERT_OK(fd4) << fd4.status().message();
  EXPECT_NE(*fd3, *fd4);
  pool_.Return(/*ok=*/false, *fd4, peer, local_ipv4b);

  mgr_->Stop();
  ta.join();
}

TEST_P(ConnPoolTest, CloseIdleClosesOnlyThatPeersIdleConnections) {
  util::Thread ta([&]() { mgr_->Start(cfg_.blocking); });
  ShortSleep();

  constexpr std::string_view local_ipv4a = "127.0.0.1";
  constexpr std::string_view local_ipv4b = "127.0.0.2";
  constexpr std::string_view local_ipv6 = "::1";
  const std::string peer4 = peer4_.ToString();
  const std::string peer6 = peer6_.ToString();
  ASSERT_NE(peer4, peer6);

  // Two idle connections to peer4 and one to peer6.
  const auto a1 = pool_.Borrow(peer4, local_ipv4a);
  ABSL_ASSERT_OK(a1) << a1.status().message();
  const auto a2 = pool_.Borrow(peer4, local_ipv4b);
  ABSL_ASSERT_OK(a2) << a2.status().message();
  const auto b1 = pool_.Borrow(peer6, local_ipv6);
  ABSL_ASSERT_OK(b1) << b1.status().message();
  pool_.Return(/*ok=*/true, *a1, peer4, local_ipv4a);
  pool_.Return(/*ok=*/true, *a2, peer4, local_ipv4b);
  pool_.Return(/*ok=*/true, *b1, peer6, local_ipv6);
  EXPECT_EQ(pool_.IdleCount(peer4), 2);
  EXPECT_EQ(pool_.IdleCount(peer6), 1);

  // A borrowed connection is not idle: it survives the close.
  const auto a3 = pool_.Borrow(peer4, local_ipv4a);
  ABSL_ASSERT_OK(a3) << a3.status().message();
  EXPECT_EQ(*a3, *a1);
  EXPECT_EQ(pool_.IdleCount(peer4), 1);
  EXPECT_EQ(pool_.CloseIdle(peer4), 1);
  EXPECT_EQ(pool_.IdleCount(peer4), 0);
  EXPECT_EQ(pool_.IdleCount(peer6), 1);
  EXPECT_GE(fcntl(*a3, F_GETFD), 0);
  EXPECT_EQ(pool_.CloseIdle(peer4), 0);

  // The survivor pools again on return, and the next borrow reuses it.
  pool_.Return(/*ok=*/true, *a3, peer4, local_ipv4a);
  EXPECT_EQ(pool_.IdleCount(peer4), 1);
  const auto a4 = pool_.Borrow(peer4, local_ipv4a);
  ABSL_ASSERT_OK(a4) << a4.status().message();
  EXPECT_EQ(*a4, *a3);
  pool_.Return(/*ok=*/true, *a4, peer4, local_ipv4a);

  mgr_->Stop();
  ta.join();
}

}  // namespace
}  // namespace peregrine::v1::testing
