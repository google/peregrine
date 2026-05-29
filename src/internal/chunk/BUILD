load("@rules_cc//cc:cc_library.bzl", "cc_library")
load("@rules_cc//cc:cc_test.bzl", "cc_test")

package(default_visibility = ["//src/internal:internal_impl"])

cc_library(
    name = "chunk",
    srcs = ["chunk.cc"],
    hdrs = ["chunk.h"],
    deps = [
        "//src/api:types",
        "//util/intops",
        "@abseil-cpp//absl/log:check",
        "@abseil-cpp//absl/strings:str_format",
        "@abseil-cpp//absl/types:span",
    ],
)

cc_test(
    name = "chunk_test",
    srcs = ["chunk_test.cc"],
    deps = [
        ":chunk",
        "//src/api:types",
        "@abseil-cpp//absl/log",
        "@abseil-cpp//absl/log:check",
        "@googletest//:gtest_main",
    ],
)

cc_library(
    name = "chunk_tracker",
    srcs = ["chunk_tracker.cc"],
    hdrs = ["chunk_tracker.h"],
    deps = [
        ":chunk",
        "//src/internal/util",
        "@abseil-cpp//absl/base:core_headers",
        "@abseil-cpp//absl/log",
        "@abseil-cpp//absl/log:check",
        "@abseil-cpp//absl/strings:str_format",
        "@abseil-cpp//absl/synchronization",
    ],
)

cc_test(
    name = "chunk_tracker_test",
    srcs = ["chunk_tracker_test.cc"],
    deps = [
        ":chunk",
        ":chunk_tracker",
        "@abseil-cpp//absl/log",
        "@abseil-cpp//absl/log:check",
        "@abseil-cpp//absl/random",
        "@abseil-cpp//absl/random:distributions",
        "@abseil-cpp//absl/time",
        "@googletest//:gtest_main",
    ],
)
