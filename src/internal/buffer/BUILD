load("@rules_cc//cc:cc_library.bzl", "cc_library")
load("@rules_cc//cc:cc_test.bzl", "cc_test")

package(default_visibility = ["//src/internal:internal_impl"])

cc_library(
    name = "buffer_tracker",
    srcs = ["buffer_tracker.cc"],
    hdrs = ["buffer_tracker.h"],
    deps = [
        "//src/api:transport_types",
        "//src/internal/base:types",
        "//src/internal/chunk:chunk_tracker",
        "//src/internal/chunk:tracker",
        "//src/util:macro",
        "@abseil-cpp//absl/base:core_headers",
        "@abseil-cpp//absl/container:flat_hash_map",
        "@abseil-cpp//absl/synchronization",
    ],
)

cc_test(
    name = "buffer_tracker_test",
    srcs = ["buffer_tracker_test.cc"],
    deps = [
        ":buffer_tracker",
        "//src/internal/chunk",
        "//src/internal/chunk:chunk_test_util",
        "//src/internal/chunk:tracker",
        "@abseil-cpp//absl/log:check",
        "@googletest//:gtest_main",
    ],
)
