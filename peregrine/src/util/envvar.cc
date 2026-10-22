#include "peregrine/src/util/envvar.h"

#include <algorithm>
#include <cstdlib>
#include <initializer_list>
#include <string>
#include <string_view>

#include "absl/strings/match.h"
#include "absl/strings/numbers.h"

namespace peregrine::util {

bool EnvVarMatchesAnyIgnoreCase(
    const std::string& name, std::initializer_list<std::string_view> values) {
  const char* ev = std::getenv(name.c_str());
  if (ev == nullptr) return false;

  return std::any_of(values.begin(), values.end(), [ev](std::string_view s) {
    return absl::EqualsIgnoreCase(ev, s);
  });
}

int ParseEnvVarInteger(const std::string& name, int default_value) {
  const char* ev = std::getenv(name.c_str());
  if (ev == nullptr) return default_value;

  int value = 0;
  return absl::SimpleAtoi(ev, &value) ? value : default_value;
}

}  // namespace peregrine::util
