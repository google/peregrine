#include "peregrine/src/util/envvar.h"

#include <stdlib.h>

#include <optional>
#include <string>

#include "gtest/gtest.h"
#include "peregrine/src/util/macro.h"

namespace peregrine::util::testing {
namespace {

// Sets an environment variable for the lifetime of this object, restoring
// its previous value, or unsetting it if it was unset, on destruction.
class ScopedEnvVar {
 public:
  ScopedEnvVar(const std::string& name, const char* value) : name_(name) {
    if (const char* old = ::getenv(name_.c_str()); old != nullptr) {
      old_value_ = old;
    }
    ::setenv(name_.c_str(), value, /*overwrite=*/1);
  }
  DISALLOW_COPY(ScopedEnvVar);
  DISALLOW_MOVE(ScopedEnvVar);
  ~ScopedEnvVar() {
    if (old_value_.has_value()) {
      ::setenv(name_.c_str(), old_value_->c_str(), /*overwrite=*/1);
    } else {
      ::unsetenv(name_.c_str());
    }
  }

 private:
  std::string name_;
  std::optional<std::string> old_value_;
};

constexpr char kEnvVar[] = "_PeregrineTestEnvVar_";

TEST(EnvVarTest, UnsetVariable) {
  ::unsetenv(kEnvVar);
  EXPECT_FALSE(IsEnvVarEnabled(kEnvVar));
  EXPECT_FALSE(IsEnvVarDisabled(kEnvVar));
  EXPECT_FALSE(IsEnvVarEnabled(""));
  EXPECT_FALSE(IsEnvVarDisabled(""));
}

TEST(EnvVarTest, IsEnabled) {
  for (const char* value : {"1", "true", "TRUE", "yes", "Yes", "on", "On"}) {
    ScopedEnvVar ev(kEnvVar, value);
    EXPECT_TRUE(IsEnvVarEnabled(kEnvVar)) << value;
    EXPECT_FALSE(IsEnvVarDisabled(kEnvVar)) << value;
  }
}

TEST(EnvVarTest, IsDisabled) {
  for (const char* value : {"0", "false", "False", "No", "no", "Off", "off"}) {
    ScopedEnvVar ev(kEnvVar, value);
    EXPECT_TRUE(IsEnvVarDisabled(kEnvVar)) << value;
    EXPECT_FALSE(IsEnvVarEnabled(kEnvVar)) << value;
  }
}

TEST(EnvVarTest, UnrecognizedValue) {
  for (const char* value : {"", "2", "maybe", "_true", "on_"}) {
    ScopedEnvVar ev(kEnvVar, value);
    EXPECT_FALSE(IsEnvVarEnabled(kEnvVar)) << "'" << value << "'";
    EXPECT_FALSE(IsEnvVarDisabled(kEnvVar)) << "'" << value << "'";
  }
}

TEST(EnvVarTest, EnvVarMatchesAnyIgnoreCase) {
  ScopedEnvVar ev(kEnvVar, "value");
  EXPECT_TRUE(EnvVarMatchesAnyIgnoreCase(kEnvVar, {"value", "VALUE"}));
  EXPECT_FALSE(EnvVarMatchesAnyIgnoreCase(kEnvVar, {"other", "OTHER"}));
  EXPECT_FALSE(EnvVarMatchesAnyIgnoreCase(kEnvVar, {}));
}

TEST(EnvVarTest, EnvVarMatchesAnyIgnoreCaseUnset) {
  ::unsetenv(kEnvVar);
  EXPECT_FALSE(EnvVarMatchesAnyIgnoreCase(kEnvVar, {"value"}));
  EXPECT_FALSE(EnvVarMatchesAnyIgnoreCase(kEnvVar, {""}));
  EXPECT_FALSE(EnvVarMatchesAnyIgnoreCase(kEnvVar, {}));
}

}  // namespace
}  // namespace peregrine::util::testing
