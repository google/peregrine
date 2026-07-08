#ifndef PEREGRINE_SRC_INTERNAL_HW_NIC_H_
#define PEREGRINE_SRC_INTERNAL_HW_NIC_H_

#include <string>
#include <vector>

#include "absl/container/flat_hash_map.h"

namespace peregrine::internal {

// Enumerates the network interface cards (NICs) on the local machine.
absl::flat_hash_map<std::string, std::vector<std::string>> EnumerateNics();

}  // namespace peregrine::internal

#endif  // PEREGRINE_SRC_INTERNAL_HW_NIC_H_
