#include "peregrine/src/util/nic_filter.h"

#include <string_view>

namespace peregrine::util {

bool IsPlatformExcludedInterface(const std::string_view ifc) { return false; }

}  // namespace peregrine::util
