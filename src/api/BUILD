# Only for public API.

load("@nanobind_bazel//:build_defs.bzl", "nanobind_extension")
load("@rules_cc//cc:cc_library.bzl", "cc_library")
load("@rules_cc//cc:cc_test.bzl", "cc_test")
load("@rules_python//python:py_test.bzl", "py_test")

package(default_visibility = [
    # TODO(yongx): make the visibility public
    "//src:__subpackages__",
    "//test:__subpackages__",
])

cc_library(
    name = "transport",
    hdrs = ["transport.h"],
    deps = [
        ":transport_types",
        "@abseil-cpp//absl/status:statusor",
        "@abseil-cpp//absl/types:span",
    ],
)

cc_library(
    name = "types",
    hdrs = ["types.h"],
    deps = [
        ":transport_types",
    ],
)

cc_library(
    name = "transport_types",
    srcs = ["transport_types.cc"],
    hdrs = ["transport_types.h"],
    deps = [
        "//third_party/gloop/util/intops",
        "@abseil-cpp//absl/log:check",
        "@abseil-cpp//absl/strings:str_format",
    ],
)

cc_test(
    name = "transport_types_test",
    srcs = ["transport_types_test.cc"],
    deps = [
        ":transport_types",
        "@abseil-cpp//absl/log",
        "@googletest//:gtest_main",
    ],
)

cc_library(
    name = "transport_util",
    srcs = ["transport_util.cc"],
    hdrs = ["transport_util.h"],
    deps = [
        ":transport",
        "//src/internal:transport_impl",
        "//src/internal/base:endpoint",
        "//src/internal/socket:acceptor",
        "@abseil-cpp//absl/base:core_headers",
        "@abseil-cpp//absl/log",
    ],
)

cc_test(
    name = "transport_util_test",
    srcs = ["transport_util_test.cc"],
    deps = [
        ":transport_util",
        "//src/util",
        "@abseil-cpp//absl/strings",
        "@googletest//:gtest_main",
    ],
)

nanobind_extension(
    name = "peregrine",
    srcs = ["python/binding.cc"],
    features = ["-use_header_modules"],
    deps = [
        ":transport",
        ":transport_types",
        ":transport_util",
        "@abseil-cpp//absl/status",
        "@abseil-cpp//absl/status:statusor",
        "@abseil-cpp//absl/types:span",
    ],
)

py_test(
    name = "python/binding_test",
    srcs = ["python/binding_test.py"],
    deps = [
        ":peregrine",
        "//src/util:util_py",
        "@abseil-py//absl/testing:absltest",
    ],
)
