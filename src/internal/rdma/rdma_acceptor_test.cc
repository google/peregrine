#include "src/internal/rdma/rdma_acceptor.h"

#include <cstddef>
#include <cstdint>
#include <vector>

#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "absl/status/status.h"
#include "src/api/transport_types.h"
#include "src/internal/base/config.h"
#include "src/internal/base/endpoint.h"
#include "src/internal/base/hostinfo.h"
#include "src/internal/control/control.h"
#include "src/internal/rdma/rdma_device_manager.h"

namespace peregrine::internal::testing {
namespace {

using ::testing::IsNull;
using ::testing::NotNull;

TEST(RdmaAcceptorTest, ReturnsNullWhenTransportTypeIsNotRdma) {
  Config config;
  config.transport_type = TransportType::kTcp;

  HostInfo self;
  self.control_plane_listener = Endpoint::Create("127.0.0.1:10000");
  SecurityCredentials creds;
  auto control = Control::Create(config, self, creds);
  ASSERT_THAT(control, NotNull());

  auto acceptor = RdmaAcceptor::Create(config, self, *control);
  EXPECT_THAT(acceptor, IsNull());
}

TEST(RdmaAcceptorTest, CreateAndRegisterMemoryWithRdmaTransport) {
  auto dev_mgr_or = RdmaDeviceManager::Create();
  if (absl::IsNotFound(dev_mgr_or.status())) {
    GTEST_SKIP()
        << "No hardware RDMA devices available in this test environment.";
  }

  Config config;
  config.transport_type = TransportType::kRdma;

  HostInfo self;
  self.control_plane_listener = Endpoint::Create("127.0.0.1:10001");
  SecurityCredentials creds;
  auto control = Control::Create(config, self, creds);
  ASSERT_THAT(control, NotNull());

  auto acceptor = RdmaAcceptor::Create(config, self, *control);
  ASSERT_THAT(acceptor, NotNull());
  EXPECT_FALSE(self.rdma_nics.empty());

  constexpr size_t kBufferSize = 4096;
  std::vector<uint8_t> buffer(kBufferSize, 0xAB);

  EXPECT_TRUE(acceptor->RegisterMemory(buffer.data(), buffer.size()).ok());
  EXPECT_TRUE(acceptor->DeregisterMemory(buffer.data()).ok());
}

}  // namespace
}  // namespace peregrine::internal::testing
