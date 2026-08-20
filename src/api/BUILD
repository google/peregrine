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
    name = "strong_int",
    hdrs = ["strong_int.h"],
    deps = ["@abseil-cpp//absl/hash"],
)

cc_test(
    name = "strong_int_test",
    srcs = ["strong_int_test.cc"],
    deps = [
        ":strong_int",
        "@abseil-cpp//absl/container:flat_hash_set",
        "@abseil-cpp//absl/log",
        "@googletest//:gtest_main",
    ],
)

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
    name = "transport_types",
    srcs = ["transport_types.cc"],
    hdrs = ["transport_types.h"],
    deps = [
        ":strong_int",
        "@abseil-cpp//absl/log:check",
        "@abseil-cpp//absl/strings:str_format",
        "@abseil-cpp//absl/types:span",
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
        "//src/internal/base:ipaddr",
        "//src/util",
        "@abseil-cpp//absl/base:core_headers",
        "@abseil-cpp//absl/log",
        "@abseil-cpp//absl/strings",
    ],
)

cc_test(
    name = "transport_util_test",
    srcs = ["transport_util_test.cc"],
    deps = [
        ":transport_util",
        "//src/util",
        "@abseil-cpp//absl/strings:str_format",
        "@googletest//:gtest_main",
    ],
)

cc_test(
    name = "simple_test",
    srcs = ["simple_test.cc"],
    deps = [
        ":transport",
        ":transport_types",
        "//src/util:app",
        "@abseil-cpp//absl/log:check",
        "@abseil-cpp//absl/time",
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

py_test(
    name = "python/simple_test_py",
    srcs = ["python/simple_test.py"],
    main = "python/simple_test.py",
    tags = ["notsan"],  # TODO(yongx): fix this
    deps = [
        ":peregrine",
        "//src/util:util_py",
        "@abseil-py//absl/logging",
        "@abseil-py//absl/testing:absltest",
    ],
)

cc_library(
    name = "socket_util",
    hdrs = ["socket_util.h"],
    deps = [
        ":transport_types",
        "//src/internal/base:types",
        "//src/internal/socket:socket_util",
        "//src/internal/socket:tcp_socket_util",
        "//src/internal/util",
        "@abseil-cpp//absl/base:core_headers",
        "@abseil-cpp//absl/log:check",
        "@abseil-cpp//absl/status",
        "@abseil-cpp//absl/strings",
        "@abseil-cpp//absl/types:span",
    ],
)

cc_test(
    name = "socket_util_test",
    srcs = ["socket_util_test.cc"],
    shard_count = 8,
    deps = [
        ":socket_util",
        ":transport_types",
        "//src/internal/base:endpoint",
        "//src/internal/base:types",
        "//src/internal/socket:socket_tcp",
        "//src/internal/util:test_util",
        "//src/util",
        "@abseil-cpp//absl/log:check",
        "@abseil-cpp//absl/status",
        "@abseil-cpp//absl/strings",
        "@abseil-cpp//absl/strings:str_format",
        "@abseil-cpp//absl/synchronization",
        "@abseil-cpp//absl/types:span",
        "@googletest//:gtest_main",
    ],
)
