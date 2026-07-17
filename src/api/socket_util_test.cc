#include "src/api/socket_util.h"

#include <sys/socket.h>
#include <sys/types.h>

#include <cstring>
#include <memory>
#include <string>
#include <thread>  // NOLINT
#include <tuple>
#include <vector>

#include "gtest/gtest.h"
#include "absl/log/check.h"
#include "absl/strings/str_format.h"
#include "absl/synchronization/notification.h"
#include "src/api/transport_types.h"
#include "src/internal/base/endpoint.h"
#include "src/internal/base/types.h"
#include "src/internal/socket/socket_tcp.h"
#include "src/internal/util/test_util.h"

namespace peregrine::testing {
namespace {

using ::peregrine::internal::Endpoint;
using ::peregrine::internal::TcpSocket;
using ::peregrine::internal::testing::IPv4Localhost;
using ::peregrine::internal::testing::IPv6Localhost;
using ::peregrine::internal::testing::TestOnly_CreateTcpSocket;
using ::peregrine::internal::testing::TestOnly_FindFreeTcpPort;
using ::testing::Combine;
using ::testing::TestParamInfo;
using ::testing::Values;

using Param = std::tuple</*family=*/int, /*size=*/size_t>;

std::string ToString(const TestParamInfo<Param>& info) {
  const int family = std::get<0>(info.param);
  const size_t size = std::get<1>(info.param);
  DCHECK(family == AF_INET || family == AF_INET6);
  return absl::StrFormat("IPv%d_%zu_bytes", family == AF_INET ? 4 : 6, size);
}

class SocketUtilTest : public ::testing::TestWithParam<Param> {
 protected:
  SocketUtilTest()
      : family_(std::get<0>(GetParam())),
        size_(std::get<1>(GetParam())),
        local_(family_ == AF_INET ? IPv4Localhost() : IPv6Localhost(),
               TestOnly_FindFreeTcpPort(family_)),
        peer_(local_),
        listener_(TestOnly_CreateTcpSocket(family_)),
        connector_(TestOnly_CreateTcpSocket(family_)) {
    DCHECK(listener_->IsValid());
    DCHECK(connector_->IsValid());
    DCHECK(!listener_->IsConnected());
    DCHECK(!connector_->IsConnected());
    DCHECK_NE(listener_->fd(), connector_->fd());
  }

 protected:
  const int family_;
  const size_t size_;
  const Endpoint local_;
  const Endpoint peer_;
  const std::unique_ptr<TcpSocket> listener_;
  const std::unique_ptr<TcpSocket> connector_;
};

INSTANTIATE_TEST_SUITE_P(, SocketUtilTest,
                         Combine(/*family=*/Values(AF_INET, AF_INET6),
                                 /*size=*/Values(128, 64UL << 20)),
                         ToString);

TEST_P(SocketUtilTest, ReadWrite) {
  // Create a big chunk of data and a recv buffer.
  std::vector<Byte> send_buf(size_, 0x01);
  std::vector<Byte> recv_buf(size_, 0x02);
  ASSERT_NE(recv_buf, send_buf);

  // First, create a server thread.
  absl::Notification server_ready;
  std::thread server([&]() {
    CHECK(listener_->Listen(local_));
    server_ready.Notify();
    DCHECK(listener_->IsBlocking());
    const internal::fd_t new_fd = listener_->Accept();

    CHECK_GE(new_fd.value(), 0);
    auto new_socket = TcpSocket::Create(new_fd, family_);
    DCHECK(new_socket->IsBlocking());
    DCHECK(new_socket->IsConnected());
    CHECK_OK(ReadExact(new_socket->fd().value(), recv_buf.data(), size_));
  });

  // Second, create a client thread.
  std::thread client([&]() {
    server_ready.WaitForNotification();
    CHECK(connector_->Connect(peer_));
    DCHECK(connector_->IsBlocking());
    DCHECK(connector_->IsConnected());
    CHECK_OK(WriteExact(connector_->fd().value(), send_buf.data(), size_));
  });

  // Wait for both threads to finish.
  client.join();
  server.join();

  // Check that the server got the client's data.
  EXPECT_EQ(recv_buf, send_buf);
}

TEST_P(SocketUtilTest, ReadVWriteV) {
  // Create a big chunk of data and a recv buffer.
  std::vector<Byte> send_buf(size_, 0x01);
  std::vector<Byte> recv_buf(size_, 0x02);
  ASSERT_NE(recv_buf, send_buf);

  // First, create a server thread.
  absl::Notification server_ready;
  std::thread server([&]() {
    CHECK(listener_->Listen(local_));
    server_ready.Notify();
    DCHECK(listener_->IsBlocking());
    const internal::fd_t new_fd = listener_->Accept();

    CHECK_GE(new_fd.value(), 0);
    auto new_socket = TcpSocket::Create(new_fd, family_);
    DCHECK(new_socket->IsBlocking());
    DCHECK(new_socket->IsConnected());

    std::vector<struct iovec> iovs;
    const size_t partial = size_ / 3;
    iovs.push_back({recv_buf.data(), partial});
    iovs.push_back({recv_buf.data() + partial, size_ - partial});
    CHECK_OK(ReadVExact(new_socket->fd().value(), iovs));
  });

  // Second, create a client thread.
  std::thread client([&]() {
    server_ready.WaitForNotification();
    CHECK(connector_->Connect(peer_));
    DCHECK(connector_->IsBlocking());
    DCHECK(connector_->IsConnected());

    std::vector<struct iovec> iovs;
    const size_t partial = size_ / 2;
    iovs.push_back({send_buf.data(), partial});
    iovs.push_back({send_buf.data() + partial, size_ - partial});
    CHECK_OK(WriteVExact(connector_->fd().value(), iovs));
  });

  // Wait for both threads to finish.
  client.join();
  server.join();

  // Check that the server got the client's data.
  EXPECT_EQ(recv_buf, send_buf);
}

}  // namespace
}  // namespace peregrine::testing
