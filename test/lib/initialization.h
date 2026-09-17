#ifndef PEREGRINE_TEST_LIB_INITIALIZATION_H_
#define PEREGRINE_TEST_LIB_INITIALIZATION_H_

#include "absl/flags/parse.h"
#include "absl/log/initialize.h"

namespace peregrine::test {

inline void Init(int& argc, char**& argv) {
  absl::ParseCommandLine(argc, argv);
  absl::InitializeLog();
}

}  // namespace peregrine::test

#endif  // PEREGRINE_TEST_LIB_INITIALIZATION_H_
