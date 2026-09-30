#ifndef PEREGRINE_SRC_UTIL_ERRNO_H_
#define PEREGRINE_SRC_UTIL_ERRNO_H_

#include "peregrine/src/util/strong_int.h"

namespace peregrine::util {

// A strong int type for `errno`.
// See https://en.wikipedia.org/wiki/Errno.h
DEFINE_STRONG_INT_TYPE(Errno, int);

}  // namespace peregrine::util

#endif  // PEREGRINE_SRC_UTIL_ERRNO_H_
