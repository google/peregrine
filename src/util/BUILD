# Utility functions for dev/testing.

load("@rules_cc//cc:cc_library.bzl", "cc_library")
load("@rules_cc//cc:cc_test.bzl", "cc_test")
load("@rules_python//python:py_library.bzl", "py_library")
load("@rules_python//python:py_test.bzl", "py_test")

package(default_visibility = [
    # Do _not_ add anything new here.
    "//src:__subpackages__",
    "//test:__subpackages__",
])

cc_library(
    name = "app",
    hdrs = ["app.h"],
    deps = [
        ":util",
        "//src/api:transport",
        "//src/api:transport_types",
        "//src/api:transport_util",
        "@abseil-cpp//absl/log:check",
        "@abseil-cpp//absl/strings",
        "@abseil-cpp//absl/types:span",
    ],
)

cc_test(
    name = "app_test",
    srcs = ["app_test.cc"],
    deps = [
        ":app",
        "@googletest//:gtest_main",
    ],
)

cc_library(
    name = "macro",
    hdrs = ["macro.h"],
)

cc_library(
    name = "nic",
    srcs = ["nic.cc"],
    hdrs = ["nic.h"],
    deps = [
        "@abseil-cpp//absl/container:flat_hash_map",
        "@abseil-cpp//absl/log",
        "@abseil-cpp//absl/log:check",
        "@abseil-cpp//absl/strings:str_format",
        "@abseil-cpp//absl/strings:string_view",
    ],
)

cc_test(
    name = "nic_test",
    srcs = ["nic_test.cc"],
    deps = [
        ":nic",
        "@abseil-cpp//absl/container:flat_hash_map",
        "@abseil-cpp//absl/log",
        "@googletest//:gtest_main",
    ],
)

cc_library(
    name = "util",
    srcs = ["util.cc"],
    hdrs = ["util.h"],
    deps = [
        "//src/api:transport_types",
        "@abseil-cpp//absl/log",
        "@abseil-cpp//absl/log:check",
        "@abseil-cpp//absl/random",
        "@abseil-cpp//absl/random:bit_gen_ref",
        "@abseil-cpp//absl/types:span",
        "@xxhash",
    ],
)

cc_test(
    name = "util_test",
    srcs = ["util_test.cc"],
    deps = [
        ":util",
        "@abseil-cpp//absl/random",
        "@googletest//:gtest_main",
    ],
)

py_library(
    name = "util_py",
    srcs = ["util.py"],
)

py_test(
    name = "util_test_py",
    srcs = ["util_test.py"],
    main = "util_test.py",
    deps = [
        ":util_py",
        "@abseil-py//absl/testing:absltest",
    ],
)
