# For integration tests only.
# Unit tests should be put in `/peregrine/src`.

load("@rules_cc//cc:cc_test.bzl", "cc_test")
load("@rules_python//python:py_test.bzl", "py_test")

package(
    default_testonly = True,
    default_visibility = ["//visibility:private"],
)

cc_test(
    name = "simple_test",
    srcs = ["simple_test.cc"],
    deps = [
        "//src/api:transport",
        "//src/api:transport_types",
        "//src/util:app",
        "@abseil-cpp//absl/log:check",
        "@abseil-cpp//absl/time",
        "@googletest//:gtest_main",
    ],
)

py_test(
    name = "simple_test_py",
    srcs = ["simple_test.py"],
    main = "simple_test.py",
    tags = ["notsan"],  # TODO(yongx): fix this
    deps = [
        "//src/api:peregrine",
        "//src/util:util_py",
        "@abseil-py//absl/logging",
        "@abseil-py//absl/testing:absltest",
    ],
)
