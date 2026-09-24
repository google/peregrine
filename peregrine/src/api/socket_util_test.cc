#include "peregrine/src/api/socket_util.h"

#include <sys/socket.h>
#include <sys/types.h>

#include <cstring>
#include <memory>
#include <string>
#include <tuple>
#include <vector>

#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "absl/log/check.h"
#include "absl/status/status.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/str_format.h"
#include "absl/synchronization/notification.h"
#include "absl/types/span.h"
#include "peregrine/src/api/transport_types.h"
#include "peregrine/src/internal/base/endpoint.h"
#include "peregrine/src/internal/base/types.h"
#include "peregrine/src/internal/socket/socket_tcp.h"
#include "peregrine/src/internal/util/test_util.h"
#include "peregrine/src/util/thread.h"
#include "peregrine/src/util/util.h"

namespace peregrine::testing {
namespace {

using internal::Endpoint;
using internal::TcpSocket;
using internal::testing::IPv4Localhost;
using internal::testing::IPv6Localhost;
using internal::testing::TestOnly_CreateTcpSocket;
using internal::testing::TestOnly_FindFreeTcpPort;
using ::testing::Combine;
using ::testing::Eq;
using ::testing::Ne;
using ::testing::Pointwise;
using ::testing::TestParamInfo;
using ::testing::Values;

constexpr bool kBlocking = true;

using Param = std::tuple</*family=*/int, /*riov=*/bool, /*wiov=*/bool>;

std::string ToString(const TestParamInfo<Param>& info) {
  const int family = std::get<0>(info.param);
  const bool read_iovec = std::get<1>(info.param);
  const bool write_iovec = std::get<2>(info.param);
  DCHECK(family == AF_INET || family == AF_INET6);
  return absl::StrFormat("IPv%d_Read%s_Write%s", family == AF_INET ? 4 : 6,
                         read_iovec ? "V" : "", write_iovec ? "V" : "");
}

class SocketUtilTest : public ::testing::TestWithParam<Param> {
 protected:
  SocketUtilTest()
      : family_(std::get<0>(GetParam())),
        read_iovec_(std::get<1>(GetParam())),
        write_iovec_(std::get<2>(GetParam())),
        local_(family_ == AF_INET ? IPv4Localhost() : IPv6Localhost(),
               TestOnly_FindFreeTcpPort(family_)),
        peer_(local_),
        listener_(TestOnly_CreateTcpSocket(family_, kBlocking)),
        connector_(TestOnly_CreateTcpSocket(family_, kBlocking)) {
    DCHECK(listener_->IsValid());
    DCHECK(connector_->IsValid());
    DCHECK(!listener_->IsConnected());
    DCHECK(!connector_->IsConnected());
    DCHECK_NE(listener_->fd(), connector_->fd());
  }

 protected:
  const int family_;
  const bool read_iovec_;
  const bool write_iovec_;
  const Endpoint local_;
  const Endpoint peer_;
  const std::unique_ptr<TcpSocket> listener_;
  const std::unique_ptr<TcpSocket> connector_;
};

INSTANTIATE_TEST_SUITE_P(, SocketUtilTest,
                         Combine(/*family=*/Values(AF_INET, AF_INET6),
                                 /*riov=*/Values(false, true),
                                 /*wiov=*/Values(false, true)),
                         ToString);

TEST_P(SocketUtilTest, ReadWrite) {
  // Create a big chunk of send/recv buffers with random data.
  constexpr size_t kSize = 64UL << 20;
  std::vector<Byte> send_buf(kSize, 0x01);
  std::vector<Byte> recv_buf(kSize, 0x00);
  util::RandomNonZero(absl::MakeSpan(send_buf));
  ASSERT_THAT(recv_buf, Pointwise(Ne(), send_buf));

  // First, create a server thread.
  absl::Notification server_ready;
  util::Thread server([&]() {
    CHECK(!listener_->Listen(local_));
    server_ready.Notify();
    DCHECK(listener_->IsBlocking());
    const internal::fd_t new_fd = listener_->Accept(/*gen_blocking=*/true);

    CHECK_GE(new_fd.value(), 0);
    auto new_socket = TcpSocket::Create(new_fd, family_);
    DCHECK(new_socket->IsBlocking());
    DCHECK(new_socket->IsConnected());

    if (read_iovec_) {
      std::vector<struct iovec> iovs;
      constexpr size_t kPartial = kSize / 3;
      iovs.push_back({recv_buf.data(), kPartial});
      iovs.push_back({recv_buf.data() + kPartial, kPartial});
      iovs.push_back({recv_buf.data() + kPartial * 2, kSize - kPartial * 2});
      CHECK_OK(ReadVExact(new_socket->fd().value(), iovs));
    } else {
      CHECK_OK(ReadExact(new_socket->fd().value(), recv_buf.data(), kSize));
    }
  });

  // Second, create a client thread.
  util::Thread client([&]() {
    server_ready.WaitForNotification();
    CHECK(!connector_->Connect(peer_));
    DCHECK(connector_->IsBlocking());
    DCHECK(connector_->IsConnected());

    if (write_iovec_) {
      std::vector<struct iovec> iovs;
      constexpr size_t kPartial = kSize / 2;
      iovs.push_back({send_buf.data(), kPartial});
      iovs.push_back({send_buf.data() + kPartial, kSize - kPartial});
      CHECK_OK(WriteVExact(connector_->fd().value(), iovs));
    } else {
      CHECK_OK(WriteExact(connector_->fd().value(), send_buf.data(), kSize));
    }
  });

  // Wait for both threads to finish.
  client.join();
  server.join();

  // Check that the recv buffer has the same data as the send.
  ASSERT_THAT(recv_buf, Pointwise(Eq(), send_buf));
}

class SocketUtilIovTest : public ::testing::Test {
 protected:
  SocketUtilIovTest() : socket_(TestOnly_CreateTcpSocket(AF_INET, kBlocking)) {
    DCHECK(socket_->IsValid());
  }

 protected:
  const std::unique_ptr<TcpSocket> socket_;
};

TEST_F(SocketUtilIovTest, ZeroIovs) {
  const int fd = socket_->fd().value();
  std::vector<struct iovec> empty_iovs;

  const absl::Status read_status = ReadVExact(fd, empty_iovs);
  EXPECT_EQ(read_status.code(), absl::StatusCode::kInvalidArgument);
  EXPECT_EQ(read_status.message(), "#iovs=0");

  const absl::Status write_status = WriteVExact(fd, empty_iovs);
  EXPECT_EQ(write_status.code(), absl::StatusCode::kInvalidArgument);
  EXPECT_EQ(write_status.message(), "#iovs=0");
}

TEST_F(SocketUtilIovTest, ExceedMaxIovs) {
  const int fd = socket_->fd().value();
  char buf = 'x';
  std::vector<struct iovec> large_iovs(IOV_MAX + 1, {&buf, 1});

  const absl::Status read_status = ReadVExact(fd, large_iovs);
  EXPECT_EQ(read_status.code(), absl::StatusCode::kInvalidArgument);
  EXPECT_EQ(read_status.message(), absl::StrCat("#iovs=", large_iovs.size()));

  const absl::Status write_status = WriteVExact(fd, large_iovs);
  EXPECT_EQ(write_status.code(), absl::StatusCode::kInvalidArgument);
  EXPECT_EQ(write_status.message(), absl::StrCat("#iovs=", large_iovs.size()));
}

}  // namespace
}  // namespace peregrine::testing
