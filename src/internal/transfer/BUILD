load("@rules_cc//cc:cc_library.bzl", "cc_library")
load("@rules_cc//cc:cc_test.bzl", "cc_test")

package(default_visibility = ["//src/internal:internal_impl"])

cc_library(
    name = "transfer",
    srcs = ["transfer.cc"],
    hdrs = ["transfer.h"],
    deps = [
        "//src/api:transport_types",
        "//src/internal:assumptions",
        "//src/internal/base:types",
        "//src/internal/channel",
        "//src/internal/channel:channel_types",
        "//src/internal/chunk",
        "//src/internal/chunk:chunk_flatbuf",
        "//src/internal/chunk:chunk_tracker",
        "//src/internal/request:request_tracker",
        "@abseil-cpp//absl/base:core_headers",
        "@abseil-cpp//absl/log",
        "@abseil-cpp//absl/log:check",
    ],
)

cc_test(
    name = "transfer_test",
    srcs = ["transfer_test.cc"],
    deps = [
        ":transfer",
        "//src/api:transport_types",
        "//src/internal:assumptions",
        "//src/internal/base:types",
        "//src/internal/channel",
        "//src/internal/channel:channel_test_util",
        "//src/internal/channel:channel_types",
        "//src/internal/chunk",
        "//src/internal/request:request_tracker",
        "//src/util",
        "@abseil-cpp//absl/log",
        "@abseil-cpp//absl/log:check",
        "@abseil-cpp//absl/random",
        "@googletest//:gtest_main",
    ],
)
