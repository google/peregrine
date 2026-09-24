#ifndef PEREGRINE_SRC_INTERNAL_UTIL_TEST_PARAM_H_
#define PEREGRINE_SRC_INTERNAL_UTIL_TEST_PARAM_H_

#include <sys/socket.h>

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
std::string ChannelTypeToString(TestChannelType t);

// Returns a string representation of a socket address family
std::string FamilyToString(int family);

// Returns a string representation of a blocking mode.
std::string BlockingToString(bool blocking);

// Parameter tuple to generate parameterized socket tests.
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

// Parameter struct to generate parameterized channel tests.
// Do not use tuple b/c no need to generate full permutations.
struct ChannelTestParam {
  TestChannelType type;
  int family;
  int error_rate;
  size_t size;

  std::string ToString() const;
};

}  // namespace peregrine::internal::testing

#endif  // PEREGRINE_SRC_INTERNAL_UTIL_TEST_PARAM_H_
