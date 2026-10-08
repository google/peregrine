#include "peregrine/src/api/socket_util.h"

#include <sys/socket.h>
#include <sys/types.h>

#include <algorithm>
#include <cstring>
#include <memory>
#include <string>
#include <tuple>
#include <vector>

#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "absl/log/check.h"
#include "absl/status/status.h"
#include "absl/status/status_matchers.h"
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

using ::absl::StatusCode::kInternal;
using ::absl_testing::StatusIs;
using internal::Endpoint;
using internal::TcpSocket;
using internal::testing::IPv4Localhost;
using internal::testing::IPv6Localhost;
using internal::testing::TestOnly_CreateTcpSocket;
using internal::testing::TestOnly_FindFreeTcpPort;
using ::testing::Combine;
using ::testing::Eq;
using ::testing::HasSubstr;
using ::testing::Ne;
using ::testing::Pointwise;
using ::testing::TestParamInfo;
using ::testing::Values;

constexpr bool kBlocking = true;

using Param = std::tuple</*family=*/int, /*riov=*/bool, /*wiov=*/bool,
                         /*timeout_ms=*/int>;

std::string ToString(const TestParamInfo<Param>& info) {
  const int family = std::get<0>(info.param);
  const bool read_iovec = std::get<1>(info.param);
  const bool write_iovec = std::get<2>(info.param);
  const int timeout_ms = std::get<3>(info.param);
  DCHECK(family == AF_INET || family == AF_INET6);
  return absl::StrFormat(
      "IPv%d_Read%s_Write%s_Timeout_%s", family == AF_INET ? 4 : 6,
      read_iovec ? "V" : "", write_iovec ? "V" : "",
      timeout_ms >= 0 ? absl::StrCat(timeout_ms, "ms") : "inf");
}

class SocketUtilTest : public ::testing::TestWithParam<Param> {
 protected:
  SocketUtilTest()
      : family_(std::get<0>(GetParam())),
        read_iovec_(std::get<1>(GetParam())),
        write_iovec_(std::get<2>(GetParam())),
        timeout_ms_(std::get<3>(GetParam())),
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

  static std::vector<struct iovec> BuildIovs(std::vector<Byte>& buf, size_t n) {
    const size_t size = buf.size();
    n = std::min(std::max(size_t{1}, n), size);
    const size_t partial = size / n;
    DCHECK_GE(partial, 1);
    std::vector<struct iovec> iovs;
    iovs.reserve(n);
    for (size_t i = 0; i < n - 1; ++i) {
      iovs.push_back({buf.data() + partial * i, partial});
    }
    iovs.push_back({buf.data() + partial * (n - 1), size - partial * (n - 1)});
    return iovs;
  }

 protected:
  const int family_;
  const bool read_iovec_;
  const bool write_iovec_;
  const int timeout_ms_;
  const Endpoint local_;
  const Endpoint peer_;
  const std::unique_ptr<TcpSocket> listener_;
  const std::unique_ptr<TcpSocket> connector_;
};

INSTANTIATE_TEST_SUITE_P(, SocketUtilTest,
                         Combine(/*family=*/Values(AF_INET, AF_INET6),
                                 /*riov=*/Values(false, true),
                                 /*wiov=*/Values(false, true),
                                 /*timeout_ms=*/Values(10'000, -1)),
                         ToString);

TEST_P(SocketUtilTest, ReadWrite) {
  // Create a small chunk of send/recv buffers with random data.
  constexpr size_t kSize = 64UL << 10;
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
    const int ret = listener_->Accept(/*gen_blocking=*/kBlocking);
    CHECK_GE(ret, 0);

    const internal::fd_t new_fd(ret);
    auto new_socket = TcpSocket::Create(new_fd, family_, kBlocking);
    DCHECK(new_socket->IsBlocking());
    DCHECK(new_socket->IsConnected());
    const int fd = new_socket->fd().value();
    if (read_iovec_) {
      std::vector<struct iovec> iovs = BuildIovs(recv_buf, 3);
      CHECK_OK(ReadVExact(fd, iovs, timeout_ms_));
    } else {
      CHECK_OK(ReadExact(fd, recv_buf.data(), kSize, timeout_ms_));
    }
  });

  // Second, create a client thread.
  util::Thread client([&]() {
    server_ready.WaitForNotification();
    CHECK(!connector_->Connect(peer_));
    DCHECK(connector_->IsBlocking());
    DCHECK(connector_->IsConnected());
    const int fd = connector_->fd().value();
    if (write_iovec_) {
      std::vector<struct iovec> iovs = BuildIovs(send_buf, 2);
      CHECK_OK(WriteVExact(fd, iovs, timeout_ms_));
    } else {
      CHECK_OK(WriteExact(fd, send_buf.data(), kSize, timeout_ms_));
    }
  });

  // Wait for both threads to finish.
  client.join();
  server.join();

  // Check that the recv buffer has the same data as the send.
  ASSERT_THAT(recv_buf, Pointwise(Eq(), send_buf));
}

TEST_P(SocketUtilTest, ReadTimeout) {
  // Create a small chunk of send/recv buffers with random data.
  constexpr size_t kSize = 64UL << 10;
  constexpr size_t kTinySize = 64UL;
  std::vector<Byte> send_buf(kSize, 0x01);
  std::vector<Byte> recv_buf(kSize, 0x00);
  util::RandomNonZero(absl::MakeSpan(send_buf));
  ASSERT_THAT(recv_buf, Pointwise(Ne(), send_buf));

  constexpr int kTimeoutMs = 10;

  // First, create a server thread.
  absl::Notification server_ready;
  absl::Notification write_done;
  util::Thread server([&]() {
    CHECK(!listener_->Listen(local_));
    server_ready.Notify();
    DCHECK(listener_->IsBlocking());
    const int ret = listener_->Accept(/*gen_blocking=*/kBlocking);
    CHECK_GE(ret, 0);

    const internal::fd_t new_fd(ret);
    auto new_socket = TcpSocket::Create(new_fd, family_, kBlocking);
    DCHECK(new_socket->IsBlocking());
    DCHECK(new_socket->IsConnected());
    const int fd = new_socket->fd().value();
    if (read_iovec_) {
      std::vector<struct iovec> iovs = BuildIovs(recv_buf, 3);
      ASSERT_THAT(ReadVExact(fd, iovs, kTimeoutMs),
                  StatusIs(kInternal, HasSubstr("timeout")));
    } else {
      ASSERT_THAT(ReadExact(fd, recv_buf.data(), kSize, kTimeoutMs),
                  StatusIs(kInternal, HasSubstr("timeout")));
    }
    write_done.WaitForNotification();
  });

  // Second, create a client thread.
  util::Thread client([&]() {
    server_ready.WaitForNotification();
    CHECK(!connector_->Connect(peer_));
    DCHECK(connector_->IsBlocking());
    DCHECK(connector_->IsConnected());
    const int fd = connector_->fd().value();
    // Write a tiny amount of the data to trigger read timeout.
    if (write_iovec_) {
      std::vector<struct iovec> iovs = {{send_buf.data(), kTinySize}};
      CHECK_OK(WriteVExact(fd, iovs));
    } else {
      CHECK_OK(WriteExact(fd, send_buf.data(), kTinySize));
    }
    write_done.Notify();
  });

  // Wait for both threads to finish.
  client.join();
  server.join();
}

TEST_P(SocketUtilTest, WriteTimeout) {
  // Create a big chunk of send/recv buffers with random data.
  constexpr size_t kSize = 64UL << 20;
  constexpr size_t kTinySize = 64UL;
  std::vector<Byte> send_buf(kSize, 0x01);
  std::vector<Byte> recv_buf(kSize, 0x00);
  util::RandomNonZero(absl::MakeSpan(send_buf));
  ASSERT_THAT(recv_buf, Pointwise(Ne(), send_buf));

  constexpr int kTimeoutMs = 10;

  // First, create a server thread.
  absl::Notification server_ready;
  absl::Notification read_done;
  util::Thread server([&]() {
    CHECK(!listener_->Listen(local_));
    server_ready.Notify();
    DCHECK(listener_->IsBlocking());
    const int ret = listener_->Accept(/*gen_blocking=*/kBlocking);
    CHECK_GE(ret, 0);

    const internal::fd_t new_fd(ret);
    auto new_socket = TcpSocket::Create(new_fd, family_, kBlocking);
    DCHECK(new_socket->IsBlocking());
    DCHECK(new_socket->IsConnected());
    const int fd = new_socket->fd().value();
    // Read a tiny amount of the data to trigger write timeout.
    if (read_iovec_) {
      std::vector<struct iovec> iovs = {{recv_buf.data(), kTinySize}};
      CHECK_OK(ReadVExact(fd, iovs));
    } else {
      CHECK_OK(ReadExact(fd, recv_buf.data(), kTinySize));
    }
    read_done.WaitForNotification();
  });

  // Second, create a client thread.
  util::Thread client([&]() {
    server_ready.WaitForNotification();
    CHECK(!connector_->Connect(peer_));
    DCHECK(connector_->IsBlocking());
    DCHECK(connector_->IsConnected());
    const int fd = connector_->fd().value();
    // Set socket SNDBUF to a tiny value so that write will block.
    const int tiny = kTinySize;
    CHECK_EQ(setsockopt(fd, SOL_SOCKET, SO_SNDBUF, &tiny, sizeof(tiny)), 0);
    if (write_iovec_) {
      std::vector<struct iovec> iovs = BuildIovs(send_buf, 3);
      EXPECT_THAT(WriteVExact(fd, iovs, kTimeoutMs),
                  StatusIs(kInternal, HasSubstr("timeout")));
    } else {
      EXPECT_THAT(WriteExact(fd, send_buf.data(), kSize, kTimeoutMs),
                  StatusIs(kInternal, HasSubstr("timeout")));
    }
    read_done.Notify();
  });

  // Wait for both threads to finish.
  client.join();
  server.join();
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
  EXPECT_EQ(read_status.code(), absl::StatusCode::kOk);

  const absl::Status write_status = WriteVExact(fd, empty_iovs);
  EXPECT_EQ(write_status.code(), absl::StatusCode::kOk);
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
