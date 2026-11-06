#ifndef PEREGRINE_SRC_API_V1_PSP_PSP_MOCK_H_
#define PEREGRINE_SRC_API_V1_PSP_PSP_MOCK_H_

#include "peregrine/src/internal/socket/psp/oss/psp_mock.h"
#include "peregrine/src/internal/socket/psp/psp_util.h"

namespace peregrine::v1::testing {

using FakePspTcpSyscalls = internal::psp::testing::FakePspTcpSyscalls;

inline void TestOnly_SetPspTcpSyscalls(void* psp_sys) {
  internal::psp::TestOnly_SetPspTcpSyscalls(psp_sys);
}

}  // namespace peregrine::v1::testing

#endif  // PEREGRINE_SRC_API_V1_PSP_PSP_MOCK_H_
