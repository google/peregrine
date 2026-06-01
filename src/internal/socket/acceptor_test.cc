#include "src/internal/socket/acceptor.h"

#include <sys/socket.h>

#include <memory>
#include <thread>  // NOLINT

#include "gtest/gtest.h"
#include "absl/log/check.h"
#include "absl/time/clock.h"
#include "absl/time/time.h"
#include "src/internal/base/endpoint.h"
#include "src/internal/socket/socket_tcp.h"
#include "src/internal/util/test_util.h"

namespace peregrine::internal::testing {
namespace {

template <int kFamily>
class TcpAcceptorTest : public ::testing::Test {
 protected:
  TcpAcceptorTest()
      : local_(TestOnly_LocalEndpoint(kFamily, /*tcp=*/true)),
        acceptor_(TcpAcceptor::Create(local_)) {}

  static void ShortSleep() { absl::SleepFor(absl::Milliseconds(300)); }

 protected:
  const Endpoint local_;
  std::unique_ptr<TcpAcceptor> acceptor_;
};

using TcpAcceptorTestIPv4 = TcpAcceptorTest<AF_INET>;
using TcpAcceptorTestIPv6 = TcpAcceptorTest<AF_INET6>;

TEST_F(TcpAcceptorTestIPv4, StartThenStop) {
  std::jthread ta([&]() {
    DCHECK(acceptor_->Socket().IsBlocking());
    acceptor_->Start();
  });

  ShortSleep();
  acceptor_->Stop();
}

TEST_F(TcpAcceptorTestIPv6, StopThenStart) {
  acceptor_->Stop();

  std::jthread ta([&]() {
    DCHECK(acceptor_->Socket().IsBlocking());
    acceptor_->Start();
  });
}

}  // namespace
}  // namespace peregrine::internal::testing
