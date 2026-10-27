#include "peregrine/src/api/v1/conn_pool.h"

#include <sys/socket.h>

#include <memory>
#include <string>

#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "absl/log/check.h"
#include "absl/status/status_matchers.h"
#include "absl/status/statusor.h"
#include "absl/time/clock.h"
#include "absl/time/time.h"
#include "peregrine/src/internal/base/hostinfo.h"
#include "peregrine/src/internal/base/nicinfo.h"
#include "peregrine/src/internal/base/types.h"
#include "peregrine/src/internal/socket/socket_util.h"
#include "peregrine/src/internal/socket/tcp_manager.h"
#include "peregrine/src/internal/util/test_param.h"
#include "peregrine/src/internal/util/test_util.h"
#include "peregrine/src/util/nic.h"
#include "peregrine/src/util/thread.h"

namespace peregrine::v1::testing {
namespace {

using ::absl_testing::IsOk;
using internal::HostInfo;
using internal::IsValidSocket;
using internal::NicInfo;
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
    CHECK(self_.IsValid());
    const auto* ni = LoopbackNic();
    CHECK_NE(ni, nullptr);
    const auto v4 = ni->PickIPv4Localhost();
    const auto v6 = ni->PickIPv6Localhost();
    CHECK(v4.has_value() || v6.has_value());
    local_ip1_ = v4.has_value() ? "127.0.0.1" : "::1";
    local_ip2_ = v4.has_value() ? "127.0.0.2" : "::2";
    peer_ = (v4.has_value() ? v4 : v6).value().ToString();
    CHECK_NE(mgr_, nullptr);
  }

  const NicInfo* LoopbackNic() const {
    for (const auto& ni : self_.data_plane_listeners) {
      if (ni.type != util::NicType::kIP) continue;
      if (ni.name == "lo") return &ni;
      for (const auto& e : ni.endpoints) {
        if (e.GetIpAddr().IsLoopback()) return &ni;
      }
    }
    return nullptr;
  }

  static void ShortSleep() { absl::SleepFor(absl::Milliseconds(100)); }

  ~ConnPoolTest() override { pool_.Close(); }

 protected:
  const SocketTestConfig cfg_;
  HostInfo self_;
  std::string local_ip1_;
  std::string local_ip2_;
  std::string peer_;
  std::unique_ptr<internal::TcpManager> mgr_;
  ConnPool pool_;
};

INSTANTIATE_TEST_SUITE_P(, ConnPoolTest,
                         Combine(/*family=*/Values(AF_INET, AF_INET6),
                                 /*blocking=*/Values(true, false)),
                         ToString);

TEST_P(ConnPoolTest, BorrowAndReturn) {
  util::Thread ta([&]() { mgr_->Start(cfg_.blocking); });
  ShortSleep();

  // Borrow and return a connection under local_ip1.
  const absl::StatusOr<int> fd1 = pool_.Borrow(peer_, local_ip1_);
  ASSERT_THAT(fd1, IsOk()) << fd1.status().message();
  pool_.Return(/*ok=*/true, *fd1, peer_, local_ip1_);

  // Borrow and return a connection under local_ip2.
  const absl::StatusOr<int> fd2 = pool_.Borrow(peer_, local_ip2_);
  ASSERT_THAT(fd2, IsOk()) << fd2.status().message();
  EXPECT_NE(*fd1, *fd2);
  pool_.Return(/*ok=*/true, *fd2, peer_, local_ip2_);

  // Borrow and return a connection under local_ip1 again.
  const absl::StatusOr<int> fd3 = pool_.Borrow(peer_, local_ip1_);
  ASSERT_THAT(fd3, IsOk()) << fd3.status().message();
  EXPECT_EQ(*fd1, *fd3);
  pool_.Return(/*ok=*/true, *fd3, peer_, local_ip1_);

  // Borrow and return a connection under local_ip2 again.
  const absl::StatusOr<int> fd4 = pool_.Borrow(peer_, local_ip2_);
  ASSERT_THAT(fd4, IsOk()) << fd4.status().message();
  EXPECT_EQ(*fd2, *fd4);
  pool_.Return(/*ok=*/false, *fd4, peer_, local_ip2_);

  mgr_->Stop();
  ta.join();
}

TEST_P(ConnPoolTest, CloseIdleConnections) {
  util::Thread ta([&]() { mgr_->Start(cfg_.blocking); });
  ShortSleep();

  // Make two connections to peer.
  const absl::StatusOr<int> a1 = pool_.Borrow(peer_, local_ip1_);
  const absl::StatusOr<int> a2 = pool_.Borrow(peer_, local_ip2_);
  ASSERT_THAT(a1, IsOk()) << a1.status().message();
  ASSERT_THAT(a2, IsOk()) << a2.status().message();
  pool_.Return(/*ok=*/true, *a1, peer_, local_ip1_);
  pool_.Return(/*ok=*/true, *a2, peer_, local_ip2_);
  EXPECT_EQ(pool_.IdleCount(peer_), 2);

  // A borrowed connection is not idle: it survives the close.
  const absl::StatusOr<int> a3 = pool_.Borrow(peer_, local_ip1_);
  ASSERT_THAT(a3, IsOk()) << a3.status().message();
  EXPECT_EQ(*a3, *a1);
  EXPECT_EQ(pool_.IdleCount(peer_), 1);
  EXPECT_EQ(pool_.CloseIdle(peer_), 1);
  EXPECT_EQ(pool_.IdleCount(peer_), 0);
  EXPECT_TRUE(IsValidSocket(internal::fd_t(*a3)));
  EXPECT_EQ(pool_.CloseIdle(peer_), 0);

  // The survivor pools again on return, and the next borrow reuses it.
  pool_.Return(/*ok=*/true, *a3, peer_, local_ip1_);
  EXPECT_EQ(pool_.IdleCount(peer_), 1);
  const absl::StatusOr<int> a4 = pool_.Borrow(peer_, local_ip1_);
  ASSERT_THAT(a4, IsOk()) << a4.status().message();
  EXPECT_EQ(*a4, *a3);
  pool_.Return(/*ok=*/false, *a4, peer_, local_ip1_);

  mgr_->Stop();
  ta.join();
}

}  // namespace
}  // namespace peregrine::v1::testing
