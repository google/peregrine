#include "peregrine/src/util/env.h"

#include <stdlib.h>

#include "gtest/gtest.h"
#include "peregrine/src/util/macro.h"

namespace peregrine::util::testing {
namespace {

// Sets environment variable for the lifetime of this object.
class ScopedEnv {
 public:
  ScopedEnv(const char* name, const char* value) : name_(name) {
    ::setenv(name_, value, /*overwrite=*/1);
  }
  DISALLOW_COPY(ScopedEnv);
  DISALLOW_MOVE(ScopedEnv);
  ~ScopedEnv() { ::unsetenv(name_); }

 private:
  const char* name_;
};

constexpr char kVar[] = "PEREGRINE_ENV_TEST_VAR";

TEST(EnvTest, UnsetVariable) {
  ::unsetenv(kVar);
  EXPECT_FALSE(util::IsEnvVarEnabled(kVar));
  EXPECT_FALSE(util::IsEnvVarDisabled(kVar));
  EXPECT_FALSE(util::IsEnvVarEnabled(""));
  EXPECT_FALSE(util::IsEnvVarDisabled(""));
}

TEST(EnvTest, IsEnabled) {
  for (const char* value : {"1", "true", "TRUE", "yes", "Yes", "on", "On"}) {
    ScopedEnv env(kVar, value);
    EXPECT_TRUE(util::IsEnvVarEnabled(kVar)) << value;
    EXPECT_FALSE(util::IsEnvVarDisabled(kVar)) << value;
  }
}

TEST(EnvTest, IsDisabled) {
  for (const char* value : {"0", "false", "False", "No", "no", "Off", "off"}) {
    ScopedEnv env(kVar, value);
    EXPECT_TRUE(util::IsEnvVarDisabled(kVar)) << value;
    EXPECT_FALSE(util::IsEnvVarEnabled(kVar)) << value;
  }
}

TEST(EnvTest, UnrecognizedValue) {
  for (const char* value : {"", "2", "maybe", "_true", "on_"}) {
    ScopedEnv env(kVar, value);
    EXPECT_FALSE(util::IsEnvVarEnabled(kVar)) << "'" << value << "'";
    EXPECT_FALSE(util::IsEnvVarDisabled(kVar)) << "'" << value << "'";
  }
}

TEST(EnvTest, EnvVarMatchesAnyIgnoreCase) {
  ScopedEnv env(kVar, "value");
  EXPECT_TRUE(util::EnvVarMatchesAnyIgnoreCase(kVar, {"value", "VALUE"}));
  EXPECT_FALSE(util::EnvVarMatchesAnyIgnoreCase(kVar, {"other", "OTHER"}));
  EXPECT_FALSE(util::EnvVarMatchesAnyIgnoreCase(kVar, {}));
}

TEST(EnvTest, EnvVarMatchesAnyIgnoreCaseUnset) {
  ::unsetenv(kVar);
  EXPECT_FALSE(util::EnvVarMatchesAnyIgnoreCase(kVar, {"value"}));
  EXPECT_FALSE(util::EnvVarMatchesAnyIgnoreCase(kVar, {""}));
  EXPECT_FALSE(util::EnvVarMatchesAnyIgnoreCase(kVar, {}));
}

}  // namespace
}  // namespace peregrine::util::testing
