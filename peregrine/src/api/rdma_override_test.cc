// Tests the temporary RDMA override in `CreateTransport()` (see
// transport_registry.h). This is a separate test binary because the registered
// RDMA transport is process-wide.

#include <sys/socket.h>

#include <cstdint>
#include <memory>
#include <string>
#include <string_view>

#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "absl/strings/str_format.h"
#include "absl/types/span.h"
#include "peregrine/src/api/options.h"
#include "peregrine/src/api/transport.h"
#include "peregrine/src/api/transport_registry.h"
#include "peregrine/src/api/transport_types.h"
#include "peregrine/src/api/transport_util.h"
#include "peregrine/src/util/util.h"

namespace peregrine {
namespace {

using ::testing::IsNull;
using ::testing::NotNull;

// What the fake RDMA transport factory last received.
struct FactoryCall {
  int count = 0;
  std::string endpoint;
  int num_parallel_connections = 0;
  bool has_recv_post_callback = false;
};

FactoryCall& LastCall() {
  static FactoryCall* call = new FactoryCall();
  return *call;
}

std::unique_ptr<Transport> CreateFakeRdmaTransport(std::string_view endpoint,
                                                   Options options) {
  FactoryCall& call = LastCall();
  ++call.count;
  call.endpoint = std::string(endpoint);
  call.num_parallel_connections = options.num_parallel_connections;
  call.has_recv_post_callback = options.recv_post_callback != nullptr;
  return nullptr;
}

PEREGRINE_REGISTER_RDMA_TRANSPORT(CreateFakeRdmaTransport);

std::string FreeLocalEndpoint() {
  const uint16_t port = util::FindFreePort(AF_INET, /*tcp=*/true);
  return absl::StrFormat("127.0.0.1:%d", port);
}

TEST(RdmaOverrideTest, StaticRegistrationIsVisible) {
  EXPECT_THAT(internal::GetRegisteredRdmaTransport(), NotNull());
}

TEST(RdmaOverrideTest, RdmaUsesRegisteredTransport) {
  const int before = LastCall().count;
  const std::string ep = FreeLocalEndpoint();
  EXPECT_THAT(CreateTransport(ep, {.transport_type = TransportType::kRdma,
                                   .num_parallel_connections = 1000,  // clamped
                                   .recv_post_callback =
                                       [](Status, const Metadata&,
                                          absl::Span<const Metadata>) {}}),
              IsNull());  // The fake factory returns nullptr.
  EXPECT_EQ(LastCall().count, before + 1);
  EXPECT_EQ(LastCall().endpoint, ep);
  EXPECT_EQ(LastCall().num_parallel_connections, 100);
  EXPECT_TRUE(LastCall().has_recv_post_callback);
}

TEST(RdmaOverrideTest, OtherTransportTypesAreUnaffected) {
  for (const TransportType t : {TransportType::kTcp, TransportType::kAny}) {
    const int before = LastCall().count;
    EXPECT_THAT(
        CreateTransport(FreeLocalEndpoint(),
                        {.transport_type = t, .num_parallel_connections = 1}),
        NotNull());  // The built-in transport.
    EXPECT_EQ(LastCall().count, before) << static_cast<int>(t);
  }
}

}  // namespace
}  // namespace peregrine
