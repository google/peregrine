load("@rules_cc//cc:cc_library.bzl", "cc_library")
load("@rules_cc//cc:cc_test.bzl", "cc_test")

package(default_visibility = ["//src/internal:internal_impl"])

cc_library(
    name = "bitset",
    srcs = ["bitset.cc"],
    hdrs = ["bitset.h"],
    deps = [
        "//src/internal/util",
        "//src/util:macro",
        "@abseil-cpp//absl/log:check",
        "@abseil-cpp//absl/strings:str_format",
    ],
)

cc_test(
    name = "bitset_test",
    srcs = ["bitset_test.cc"],
    deps = [
        ":bitset",
        "//src/util",
        "@abseil-cpp//absl/log",
        "@abseil-cpp//absl/log:check",
        "@abseil-cpp//absl/random",
        "@googletest//:gtest_main",
    ],
)

cc_library(
    name = "metric_counter",
    hdrs = ["metric_counter.h"],
    deps = [
        "//src/util:macro",
    ],
)

cc_test(
    name = "metric_counter_test",
    srcs = ["metric_counter_test.cc"],
    deps = [
        ":metric_counter",
        "@googletest//:gtest_main",
    ],
)

cc_library(
    name = "metric_histogram",
    hdrs = ["metric_histogram.h"],
    deps = [
        ":metric_counter",
        "//src/util:macro",
        "@abseil-cpp//absl/base:core_headers",
        "@abseil-cpp//absl/log:check",
    ],
)

cc_test(
    name = "metric_histogram_test",
    srcs = ["metric_histogram_test.cc"],
    deps = [
        ":metric_histogram",
        "@googletest//:gtest_main",
    ],
)
