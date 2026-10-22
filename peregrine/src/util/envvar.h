#ifndef PEREGRINE_SRC_UTIL_ENVVAR_H_
#define PEREGRINE_SRC_UTIL_ENVVAR_H_

#include <initializer_list>
#include <string>
#include <string_view>

namespace peregrine::util {

// Returns true iff the environment variable `name` is set to one of `values`,
// case-insensitively.
bool EnvVarMatchesAnyIgnoreCase(const std::string& name,
                                std::initializer_list<std::string_view> values);

// Returns true iff the environment variable `name` is set to a truthy value
// case-insensitively: {"1", "true", "yes", "on"}.
inline bool IsEnvVarEnabled(const std::string& name) {
  return EnvVarMatchesAnyIgnoreCase(name, {"1", "true", "yes", "on"});
}

// Returns true iff the environment variable `name` is set to a falsy value
// case-insensitively: {"0", "false", "no", "off"}.
inline bool IsEnvVarDisabled(const std::string& name) {
  return EnvVarMatchesAnyIgnoreCase(name, {"0", "false", "no", "off"});
}

// Returns the integer value of the environment variable `name`, or
// `default_value` if it is not set or cannot be parsed as an integer.
int ParseEnvVarInteger(const std::string& name, int default_value);

}  // namespace peregrine::util

#endif  // PEREGRINE_SRC_UTIL_ENVVAR_H_
