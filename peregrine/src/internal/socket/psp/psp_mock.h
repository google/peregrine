#ifndef PEREGRINE_SRC_INTERNAL_SOCKET_PSP_PSP_MOCK_H_
#define PEREGRINE_SRC_INTERNAL_SOCKET_PSP_PSP_MOCK_H_

#include <memory>

namespace peregrine::internal::psp::testing {

class FakePspTcpSyscalls {
 public:
  static std::unique_ptr<FakePspTcpSyscalls> Create() {
    return std::make_unique<FakePspTcpSyscalls>();
  }
};

}  // namespace peregrine::internal::psp::testing

#endif  // PEREGRINE_SRC_INTERNAL_SOCKET_PSP_PSP_MOCK_H_
