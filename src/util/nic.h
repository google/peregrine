#ifndef PEREGRINE_SRC_UTIL_NIC_H_
#define PEREGRINE_SRC_UTIL_NIC_H_

#include <string>
#include <vector>

#include "absl/container/flat_hash_map.h"

namespace peregrine::util {

// Enumerates the network interface cards (NICs) on the local machine.
absl::flat_hash_map<std::string, std::vector<std::string>> EnumerateNics();

}  // namespace peregrine::util

#endif  // PEREGRINE_SRC_UTIL_NIC_H_
