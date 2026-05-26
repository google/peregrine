#ifndef PEREGRINE_UTIL_UTIL_H_
#define PEREGRINE_UTIL_UTIL_H_

#include <stdint.h>

namespace peregrine::util {

// Finds an unused port in the range [10'000, 65'535], inclusively. Returns
// the port number if successful, or 0 if failed. Note: there is no guarantee
// that the found port is still available when the caller actually uses it.
uint16_t FindFreePort(int family, bool tcp);

}  // namespace peregrine::util

#endif  // PEREGRINE_UTIL_UTIL_H_
