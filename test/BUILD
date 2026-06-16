# For integration tests only.
# Unit tests should be put in `/peregrine/src`.

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
        "//src/api:transport",
        "//src/api:types",
        "//src/util:app",
        "@abseil-cpp//absl/log:check",
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
        "//src/util:util_py",
        "@abseil-py//absl/logging",
        "@abseil-py//absl/testing:absltest",
    ],
)
