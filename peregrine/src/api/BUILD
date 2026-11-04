# Public API.

load("@nanobind_bazel//:build_defs.bzl", "nanobind_extension")
load("@rules_cc//cc:cc_library.bzl", "cc_library")
load("@rules_cc//cc:cc_test.bzl", "cc_test")
load("@rules_python//python:py_test.bzl", "py_test")

package(default_visibility = ["//visibility:public"])

cc_library(
    name = "transport",
    hdrs = ["transport.h"],
    deps = [
        ":transport_metrics",
        ":transport_trace",
        ":transport_types",
        "@abseil-cpp//absl/functional:any_invocable",
        "@abseil-cpp//absl/status",
        "@abseil-cpp//absl/status:statusor",
        "@abseil-cpp//absl/types:span",
    ],
)

cc_test(
    name = "transport_test",
    srcs = ["transport_test.cc"],
    shard_count = 2,
    deps = [
        ":transport",
        ":transport_metrics",
        ":transport_trace",
        ":transport_types",
        "//peregrine/src/util:app",
        "@abseil-cpp//absl/base:core_headers",
        "@abseil-cpp//absl/functional:any_invocable",
        "@abseil-cpp//absl/log:check",
        "@abseil-cpp//absl/status:statusor",
        "@abseil-cpp//absl/strings:str_format",
        "@abseil-cpp//absl/synchronization",
        "@abseil-cpp//absl/time",
        "@abseil-cpp//absl/types:span",
        "@googletest//:gtest_main",
    ],
)

cc_library(
    name = "transport_metrics",
    hdrs = ["transport_metrics.h"],
    deps = [
        "@abseil-cpp//absl/strings",
        "@abseil-cpp//absl/strings:str_format",
    ],
)

cc_test(
    name = "transport_metrics_test",
    srcs = ["transport_metrics_test.cc"],
    deps = [
        ":transport_metrics",
        "@googletest//:gtest_main",
    ],
)

cc_library(
    name = "transport_trace",
    hdrs = ["transport_trace.h"],
)

cc_library(
    name = "transport_types",
    srcs = ["transport_types.cc"],
    hdrs = ["transport_types.h"],
    deps = [
        "//peregrine/src/util:strong_int",
        "@abseil-cpp//absl/functional:any_invocable",
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
    name = "options",
    hdrs = ["options.h"],
    deps = [":transport_types"],
)

cc_library(
    name = "transport_util",
    srcs = ["transport_util.cc"],
    hdrs = ["transport_util.h"],
    deps = [
        ":options",
        ":transport",
        ":transport_registry",
        ":transport_types",
        "//peregrine/src/internal:transport_impl",
        "//peregrine/src/internal/base:config",
        "//peregrine/src/internal/base:endpoint",
        "@grpc//:grpc++",
    ],
)

cc_test(
    name = "transport_util_test",
    srcs = ["transport_util_test.cc"],
    deps = [
        ":transport_types",
        ":transport_util",
        "//peregrine/src/util",
        "@abseil-cpp//absl/strings:str_format",
        "@googletest//:gtest_main",
    ],
)

nanobind_extension(
    name = "peregrine",
    srcs = ["python/binding.cc"],
    features = ["-use_header_modules"],
    deps = [
        ":transport",
        ":transport_metrics",
        ":transport_trace",
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
        "//peregrine/src/util:util_py",
        "@abseil-py//absl/testing:absltest",
    ],
)

py_test(
    name = "python/transport_test_py",
    srcs = ["python/transport_test.py"],
    main = "python/transport_test.py",
    tags = ["notsan"],  # TODO(yongx): fix this
    deps = [
        ":peregrine",
        "//peregrine/src/util:util_py",
        "@abseil-py//absl/logging",
        "@abseil-py//absl/testing:absltest",
    ],
)

# DEPRECATED / TEMPORARY: lets a separately linked transport stand in for the
# built-in RDMA transport. See transport_registry.h.
cc_library(
    name = "transport_registry",
    srcs = ["transport_registry.cc"],
    hdrs = ["transport_registry.h"],
    deps = [
        ":options",
        ":transport",
        "@abseil-cpp//absl/base:core_headers",
        "@abseil-cpp//absl/base:no_destructor",
        "@abseil-cpp//absl/functional:any_invocable",
        "@abseil-cpp//absl/log:check",
        "@abseil-cpp//absl/status",
        "@abseil-cpp//absl/synchronization",
    ],
)

cc_test(
    name = "transport_registry_test",
    srcs = ["transport_registry_test.cc"],
    deps = [
        ":options",
        ":transport",
        ":transport_registry",
        "@abseil-cpp//absl/status",
        "@abseil-cpp//absl/status:status_matchers",
        "@googletest//:gtest_main",
    ],
)

# Separate binary: the registered RDMA transport is process-wide.
cc_test(
    name = "rdma_override_test",
    srcs = ["rdma_override_test.cc"],
    deps = [
        ":options",
        ":transport",
        ":transport_registry",
        ":transport_types",
        ":transport_util",
        "//peregrine/src/util",
        "@abseil-cpp//absl/strings:str_format",
        "@abseil-cpp//absl/types:span",
        "@googletest//:gtest_main",
    ],
)
