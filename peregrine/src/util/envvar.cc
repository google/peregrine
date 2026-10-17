#include "peregrine/src/util/envvar.h"

#include <algorithm>
#include <cstdlib>
#include <initializer_list>
#include <string>
#include <string_view>

#include "absl/strings/match.h"

namespace peregrine::util {

bool EnvVarMatchesAnyIgnoreCase(
    const std::string& name, std::initializer_list<std::string_view> values) {
  const char* ev = std::getenv(name.c_str());
  if (ev == nullptr) return false;

  return std::any_of(values.begin(), values.end(), [ev](std::string_view s) {
    return absl::EqualsIgnoreCase(ev, s);
  });
}

}  // namespace peregrine::util
