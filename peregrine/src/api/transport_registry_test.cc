#include "peregrine/src/api/transport_registry.h"

#include <memory>
#include <string_view>

#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "absl/status/status.h"
#include "absl/status/status_matchers.h"
#include "peregrine/src/api/options.h"
#include "peregrine/src/api/transport.h"

// This test intentionally exercises the deprecated registration API.
#pragma GCC diagnostic ignored "-Wdeprecated-declarations"

namespace peregrine {
namespace {

using ::absl_testing::IsOk;
using ::absl_testing::StatusIs;
using ::testing::IsNull;
using ::testing::NotNull;

// A factory that counts its calls via `*calls` and always returns nullptr.
TransportFactory CountingFactory(int* calls) {
  return [calls](std::string_view, Options) -> std::unique_ptr<Transport> {
    ++*calls;
    return nullptr;
  };
}

// The registered RDMA transport is process-wide, so the whole lifecycle is
// checked in order within one test.
TEST(TransportRegistryTest, RegisterRdmaTransportLifecycle) {
  // Nothing is registered initially.
  EXPECT_THAT(internal::GetRegisteredRdmaTransport(), IsNull());

  // A null factory is rejected and leaves nothing registered.
  EXPECT_THAT(RegisterRdmaTransport(nullptr),
              StatusIs(absl::StatusCode::kInvalidArgument));
  EXPECT_THAT(internal::GetRegisteredRdmaTransport(), IsNull());

  // A registered factory is retrievable and callable.
  int first_calls = 0;
  ASSERT_THAT(RegisterRdmaTransport(CountingFactory(&first_calls)), IsOk());
  const TransportFactory* f = internal::GetRegisteredRdmaTransport();
  ASSERT_THAT(f, NotNull());
  EXPECT_THAT((*f)("127.0.0.1:0", Options{}), IsNull());
  EXPECT_EQ(first_calls, 1);

  // A second registration fails and keeps the original.
  int second_calls = 0;
  EXPECT_THAT(RegisterRdmaTransport(CountingFactory(&second_calls)),
              StatusIs(absl::StatusCode::kAlreadyExists));
  EXPECT_EQ(internal::GetRegisteredRdmaTransport(), f);
  (*f)("127.0.0.1:0", Options{});
  EXPECT_EQ(first_calls, 2);
  EXPECT_EQ(second_calls, 0);
}

std::unique_ptr<Transport> CreateNullTransport(std::string_view, Options) {
  return nullptr;
}

TEST(TransportRegistryDeathTest, DuplicateStaticRegistrationCrashes) {
  // Registers twice so the second one fails regardless of prior state.
  EXPECT_DEATH(
      {
        internal::RegisterRdmaTransportStatically(CreateNullTransport);
        internal::RegisterRdmaTransportStatically(CreateNullTransport);
      },
      "already registered");
}

}  // namespace
}  // namespace peregrine
