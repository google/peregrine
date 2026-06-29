load("@rules_cc//cc:cc_library.bzl", "cc_library")
load("@rules_cc//cc:cc_test.bzl", "cc_test")

package(default_visibility = ["//src/internal:internal_impl"])

cc_library(
    name = "request_tracker",
    srcs = ["request_tracker.cc"],
    hdrs = ["request_tracker.h"],
    deps = [
        "//src/api:transport_types",
        "//src/internal/base:types",
        "//src/internal/chunk:chunk_tracker",
        "//src/util:macro",
        "@abseil-cpp//absl/base:core_headers",
        "@abseil-cpp//absl/container:flat_hash_map",
        "@abseil-cpp//absl/synchronization",
    ],
)

cc_test(
    name = "request_tracker_test",
    srcs = ["request_tracker_test.cc"],
    deps = [
        ":request_tracker",
        "//src/api:transport_types",
        "//src/internal/chunk",
        "//src/internal/chunk:chunk_test_util",
        "//src/internal/chunk:chunk_tracker",
        "@abseil-cpp//absl/log:check",
        "@googletest//:gtest_main",
    ],
)
