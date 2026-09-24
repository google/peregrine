load("@rules_cc//cc:cc_library.bzl", "cc_library")
load("@rules_cc//cc:cc_test.bzl", "cc_test")

package(default_visibility = ["//peregrine/src/internal:internal_impl"])

cc_library(
    name = "transfer",
    srcs = ["transfer.cc"],
    hdrs = ["transfer.h"],
    deps = [
        "//peregrine/src/api:transport_types",
        "//peregrine/src/internal:assumptions",
        "//peregrine/src/internal/base:types",
        "//peregrine/src/internal/channel",
        "//peregrine/src/internal/channel:channel_types",
        "//peregrine/src/internal/chunk",
        "//peregrine/src/internal/chunk:chunk_flatbuf",
        "//peregrine/src/internal/chunk:chunk_tracker",
        "//peregrine/src/internal/request:request_tracker",
        "@abseil-cpp//absl/base:core_headers",
        "@abseil-cpp//absl/log",
        "@abseil-cpp//absl/log:check",
    ],
)

cc_test(
    name = "transfer_test",
    srcs = ["transfer_test.cc"],
    shard_count = 10,
    deps = [
        ":transfer",
        "//peregrine/src/api:transport_types",
        "//peregrine/src/internal:assumptions",
        "//peregrine/src/internal/base:types",
        "//peregrine/src/internal/channel",
        "//peregrine/src/internal/channel:channel_test_util",
        "//peregrine/src/internal/chunk",
        "//peregrine/src/internal/metrics:engine_metrics",
        "//peregrine/src/internal/request:request_tracker",
        "//peregrine/src/internal/util:test_param",
        "//peregrine/src/util",
        "//peregrine/src/util:thread",
        "@abseil-cpp//absl/log",
        "@abseil-cpp//absl/log:check",
        "@abseil-cpp//absl/random",
        "@abseil-cpp//absl/time",
        "@googletest//:gtest_main",
    ],
)
