load("@rules_cc//cc:cc_library.bzl", "cc_library")
load("@rules_cc//cc:cc_test.bzl", "cc_test")

package(default_visibility = ["//peregrine/src/internal:internal_impl"])

cc_library(
    name = "bitset",
    srcs = ["bitset.cc"],
    hdrs = ["bitset.h"],
    deps = [
        "//peregrine/src/util:macro",
        "@abseil-cpp//absl/log:check",
        "@abseil-cpp//absl/strings:str_format",
    ],
)

cc_test(
    name = "bitset_test",
    srcs = ["bitset_test.cc"],
    deps = [
        ":bitset",
        "//peregrine/src/util",
        "@abseil-cpp//absl/log",
        "@abseil-cpp//absl/log:check",
        "@abseil-cpp//absl/random",
        "@googletest//:gtest_main",
    ],
)

cc_library(
    name = "iovec_cursor",
    hdrs = ["iovec_cursor.h"],
    deps = [
        "//peregrine/src/internal/base:types",
        "//peregrine/src/internal/util",
        "//peregrine/src/util:macro",
        "@abseil-cpp//absl/container:inlined_vector",
        "@abseil-cpp//absl/log:check",
        "@abseil-cpp//absl/strings:str_format",
        "@abseil-cpp//absl/types:span",
    ],
)

cc_test(
    name = "iovec_cursor_test",
    srcs = ["iovec_cursor_test.cc"],
    deps = [
        ":iovec_cursor",
        "//peregrine/src/internal/base:types",
        "@abseil-cpp//absl/log",
        "@googletest//:gtest_main",
    ],
)

cc_library(
    name = "metric_counter",
    hdrs = ["metric_counter.h"],
    deps = [
        "//peregrine/src/util:macro",
    ],
)

cc_test(
    name = "metric_counter_test",
    srcs = ["metric_counter_test.cc"],
    deps = [
        ":metric_counter",
        "//peregrine/src/util:thread",
        "@googletest//:gtest_main",
    ],
)

cc_library(
    name = "log2_histogram",
    hdrs = ["log2_histogram.h"],
    deps = [
        ":metric_counter",
        "//peregrine/src/api:transport_metrics",
        "//peregrine/src/util:macro",
        "@abseil-cpp//absl/log:check",
    ],
)

cc_test(
    name = "log2_histogram_test",
    srcs = ["log2_histogram_test.cc"],
    deps = [
        ":log2_histogram",
        "//peregrine/src/api:transport_metrics",
        "//peregrine/src/util:thread",
        "@googletest//:gtest_main",
    ],
)

cc_library(
    name = "rate_limiter",
    srcs = ["rate_limiter.cc"],
    hdrs = ["rate_limiter.h"],
    deps = [
        "//peregrine/src/util:clock",
        "//peregrine/src/util:strong_int",
        "@abseil-cpp//absl/base:core_headers",
        "@abseil-cpp//absl/log:check",
        "@abseil-cpp//absl/synchronization",
        "@abseil-cpp//absl/time",
    ],
)

cc_test(
    name = "rate_limiter_test",
    srcs = ["rate_limiter_test.cc"],
    deps = [
        ":rate_limiter",
        "//peregrine/src/util:clock",
        "//peregrine/src/util:thread",
        "@abseil-cpp//absl/time",
        "@googletest//:gtest_main",
    ],
)
