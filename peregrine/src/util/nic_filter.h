#ifndef PEREGRINE_SRC_UTIL_NIC_FILTER_H_
#define PEREGRINE_SRC_UTIL_NIC_FILTER_H_

#include <string_view>

namespace peregrine::util {

// Returns true if the interface should be excluded from data-plane NIC.
bool IsPlatformExcludedInterface(std::string_view ifc);

}  // namespace peregrine::util

#endif  // PEREGRINE_SRC_UTIL_NIC_FILTER_H_
