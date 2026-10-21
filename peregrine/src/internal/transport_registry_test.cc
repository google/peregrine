#include "peregrine/src/internal/transport_registry.h"

#include <memory>

#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "absl/status/status.h"
#include "absl/status/status_matchers.h"
#include "peregrine/src/api/transport.h"
#include "peregrine/src/api/transport_types.h"
#include "peregrine/src/internal/base/config.h"
#include "peregrine/src/internal/base/endpoint.h"

namespace peregrine::internal {
namespace {

using ::absl_testing::IsOk;
using ::absl_testing::StatusIs;
using ::testing::IsNull;
using ::testing::NotNull;

// The registry is process-wide, so each test uses its own transport type.
TransportType FakeType(int v) { return static_cast<TransportType>(v); }

TransportRegistration MakeRegistration(int* create_calls, bool supported) {
  return {
      .create = [create_calls](
                    const Config&, const Endpoint&,
                    SecurityCredentials) -> std::unique_ptr<Transport> {
        ++*create_calls;
        return nullptr;
      },
      .is_supported = [supported] { return supported; },
  };
}

TEST(TransportRegistryTest, UnregisteredTypeIsNotFound) {
  EXPECT_THAT(FindRegisteredTransport(TransportType::kTcp), IsNull());
  EXPECT_THAT(FindRegisteredTransport(TransportType::kRdma), IsNull());
  EXPECT_THAT(FindRegisteredTransport(FakeType(200)), IsNull());
}

TEST(TransportRegistryTest, RegisterAndFind) {
  int create_calls = 0;
  ASSERT_THAT(
      RegisterTransport(FakeType(201), MakeRegistration(&create_calls, true)),
      IsOk());

  const TransportRegistration* r = FindRegisteredTransport(FakeType(201));
  ASSERT_THAT(r, NotNull());
  EXPECT_TRUE(r->is_supported());
  EXPECT_THAT(r->create(Config{}, Endpoint{}, SecurityCredentials{}), IsNull());
  EXPECT_EQ(create_calls, 1);

  // The pointer is stable across further registrations.
  int other_calls = 0;
  ASSERT_THAT(
      RegisterTransport(FakeType(202), MakeRegistration(&other_calls, false)),
      IsOk());
  EXPECT_EQ(FindRegisteredTransport(FakeType(201)), r);
}

TEST(TransportRegistryTest, DuplicateRegistrationFails) {
  int first_calls = 0;
  int second_calls = 0;
  ASSERT_THAT(
      RegisterTransport(FakeType(203), MakeRegistration(&first_calls, true)),
      IsOk());
  EXPECT_THAT(
      RegisterTransport(FakeType(203), MakeRegistration(&second_calls, false)),
      StatusIs(absl::StatusCode::kAlreadyExists));

  // The original registration is kept.
  const TransportRegistration* r = FindRegisteredTransport(FakeType(203));
  ASSERT_THAT(r, NotNull());
  EXPECT_TRUE(r->is_supported());
}

TEST(TransportRegistryTest, IncompleteRegistrationFails) {
  EXPECT_THAT(RegisterTransport(FakeType(204), TransportRegistration{}),
              StatusIs(absl::StatusCode::kInvalidArgument));
  EXPECT_THAT(FindRegisteredTransport(FakeType(204)), IsNull());
}

// Registered at static-initialization time, before any test runs.
TransportRegistration MakeStaticRegistration() {
  return {
      .create = [](const Config&, const Endpoint&, SecurityCredentials)
          -> std::unique_ptr<Transport> { return nullptr; },
      .is_supported = [] { return true; },
  };
}
PEREGRINE_REGISTER_TRANSPORT(static_fake, FakeType(210),
                             MakeStaticRegistration);

TEST(TransportRegistryTest, StaticRegistrationIsAvailable) {
  const TransportRegistration* r = FindRegisteredTransport(FakeType(210));
  ASSERT_THAT(r, NotNull());
  EXPECT_TRUE(r->is_supported());
}

TEST(TransportRegistryDeathTest, DuplicateStaticRegistrationCrashes) {
  EXPECT_DEATH(
      RegisterTransportStatically(FakeType(210), MakeStaticRegistration),
      "already registered");
}

}  // namespace
}  // namespace peregrine::internal
