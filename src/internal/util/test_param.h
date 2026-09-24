#ifndef PEREGRINE_SRC_INTERNAL_UTIL_TEST_PARAM_H_
#define PEREGRINE_SRC_INTERNAL_UTIL_TEST_PARAM_H_

#include <cstddef>
#include <string>
#include <tuple>

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

// Returns a string representation of a blocking mode.
std::string BlockingToString(bool blocking);

// Parameter tuple to generate test combinations for parameterized tests.
using SocketTestParam = std::tuple</*family=*/int, /*blocking=*/bool>;
// Parameter struct to use field names, not `std::get<n>`, in tests.
struct SocketTestConfig {
  int family;
  bool blocking;

  explicit SocketTestConfig(const SocketTestParam& p)
      : family(std::get<0>(p)), blocking(std::get<1>(p)) {}

  std::string ToString() const;
};
inline std::string ToString(const SocketTestParam& p) {
  return SocketTestConfig(p).ToString();
}

// TODO: follow the way SocketTestParam/SocketTestConfig is defined.
// For example, see the code in socket/{acceptor,connector}_test.cc.

// Parameter struct for test channel type, address family, and error rate.
struct TestChannelErrorParam {
  TestChannelType type;
  int family = 0;
  int error_rate = 0;
};

// Returns a string representation for TestChannelErrorParam in tests.
std::string ToString(const TestChannelErrorParam& p);

// Parameter struct for test channel type, address family, and buffer size.
struct TestChannelSizeParam {
  TestChannelType type;
  int family = 0;
  size_t size = 0;
};

// Returns a string representation for TestChannelSizeParam in tests.
std::string ToString(const TestChannelSizeParam& p);

}  // namespace peregrine::internal::testing

#endif  // PEREGRINE_SRC_INTERNAL_UTIL_TEST_PARAM_H_
