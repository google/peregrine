# For integration tests only.
# Unit tests should be put in `/peregrine/src`.

load("@rules_cc//cc:cc_library.bzl", "cc_library")
load("@rules_cc//cc:cc_test.bzl", "cc_test")
load("@rules_python//python:py_test.bzl", "py_test")

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
        "@abseil-cpp//absl/strings:string_view",
        "@abseil-cpp//absl/time",
        "@googletest//:gtest_main",
    ],
)

py_test(
    name = "integration/simple_test_py",
    srcs = ["integration/simple_test.py"],
    main = "integration/simple_test.py",
    deps = [
        "//src/api:peregrine",
        "//testing/pybase",
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
