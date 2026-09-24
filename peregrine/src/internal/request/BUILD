load("@rules_cc//cc:cc_library.bzl", "cc_library")
load("@rules_cc//cc:cc_test.bzl", "cc_test")

package(default_visibility = ["//peregrine/src/internal:internal_impl"])

cc_library(
    name = "request_tracker",
    srcs = ["request_tracker.cc"],
    hdrs = ["request_tracker.h"],
    deps = [
        "//peregrine/src/api:transport_types",
        "//peregrine/src/internal/base:types",
        "//peregrine/src/internal/chunk",
        "//peregrine/src/internal/chunk:chunk_tracker",
        "//peregrine/src/internal/metrics:engine_metrics",
        "//peregrine/src/util:macro",
        "@abseil-cpp//absl/base:core_headers",
        "@abseil-cpp//absl/container:flat_hash_map",
        "@abseil-cpp//absl/functional:any_invocable",
        "@abseil-cpp//absl/log:check",
        "@abseil-cpp//absl/synchronization",
        "@abseil-cpp//absl/time",
        "@abseil-cpp//absl/types:span",
    ],
)

cc_test(
    name = "request_tracker_test",
    srcs = ["request_tracker_test.cc"],
    deps = [
        ":request_tracker",
        "//peregrine/src/api:transport_types",
        "//peregrine/src/internal/base:types",
        "//peregrine/src/internal/chunk",
        "//peregrine/src/internal/chunk:chunk_test_util",
        "//peregrine/src/internal/chunk:chunk_tracker",
        "//peregrine/src/internal/metrics:engine_metrics",
        "@abseil-cpp//absl/log:check",
        "@abseil-cpp//absl/time",
        "@googletest//:gtest_main",
    ],
)
