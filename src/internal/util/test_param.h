#ifndef PEREGRINE_SRC_INTERNAL_UTIL_TEST_PARAM_H_
#define PEREGRINE_SRC_INTERNAL_UTIL_TEST_PARAM_H_

#include <cstddef>
#include <string>

namespace peregrine::internal::testing {

enum class TestChannelType {
  kTcp,
  kUdp,
  kMemStream,
  kMemMsg,
};

// Returns a string representation for the test channel type.
std::string ToString(TestChannelType t);

// Returns a string representation of a socket address family
std::string FamilyToString(int family);

// Parameter struct for test channel type, address family, and error rate.
struct TestChannelErrorParam {
  TestChannelType type;
  int family = 0;
  int error_rate = 0;
};

// Returns a string representation for TestChannelErrorParam in tests.
std::string ToString(const TestChannelErrorParam& param);

// Parameter struct for test channel type, address family, and buffer size.
struct TestChannelSizeParam {
  TestChannelType type;
  int family = 0;
  size_t size = 0;
};

// Returns a string representation for TestChannelSizeParam in tests.
std::string ToString(const TestChannelSizeParam& param);

}  // namespace peregrine::internal::testing

#endif  // PEREGRINE_SRC_INTERNAL_UTIL_TEST_PARAM_H_
