# For integration tests only.
# Unit tests should be put in `/peregrine/src`.

load("@rules_cc//cc:cc_library.bzl", "cc_library")
load("@rules_cc//cc:cc_test.bzl", "cc_test")

package(
    default_testonly = True,
    default_visibility = ["//visibility:private"],
)

cc_test(
    name = "integration/simple_test",
    srcs = ["integration/simple_test.cc"],
    deps = [
        ":integration/test_util",
        "//src/api:transport",
        "//src/api:types",
        "@abseil-cpp//absl/log:check",
        "@abseil-cpp//absl/time",
        "@googletest//:gtest_main",
    ],
)

cc_library(
    name = "integration/test_util",
    hdrs = ["integration/test_util.h"],
    deps = [
        "//src/api:transport",
        "//src/api:transport_util",
        "//src/api:types",
        "@abseil-cpp//absl/log:check",
        "@abseil-cpp//absl/types:span",
    ],
)
