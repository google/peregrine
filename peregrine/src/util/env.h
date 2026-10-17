#ifndef PEREGRINE_SRC_UTIL_ENV_H_
#define PEREGRINE_SRC_UTIL_ENV_H_

#include <initializer_list>
#include <string>
#include <string_view>

namespace peregrine::util {

// Returns true iff `name` is set to one of `values`, case-insensitively.
bool EnvVarMatchesAnyIgnoreCase(const std::string& name,
                                std::initializer_list<std::string_view> values);

// Returns true when environment variable `name` is set to a truthy value
// case-insensitively: {"1", "true", "yes", "on"}.
inline bool IsEnvVarEnabled(const std::string& name) {
  return EnvVarMatchesAnyIgnoreCase(name, {"1", "true", "yes", "on"});
}

// Returns true when environment variable `name` is set to a falsy value
// case-insensitively: {"0", "false", "no", "off"}.
inline bool IsEnvVarDisabled(const std::string& name) {
  return EnvVarMatchesAnyIgnoreCase(name, {"0", "false", "no", "off"});
}

}  // namespace peregrine::util

#endif  // PEREGRINE_SRC_UTIL_ENV_H_
