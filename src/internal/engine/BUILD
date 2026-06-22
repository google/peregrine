load("@rules_cc//cc:cc_library.bzl", "cc_library")
load("@rules_cc//cc:cc_test.bzl", "cc_test")

package(default_visibility = ["//src/internal:internal_impl"])

cc_library(
    name = "engine",
    srcs = ["engine.cc"],
    hdrs = ["engine.h"],
    deps = [
        "//src/api:transport_types",
        "//src/internal:assumptions",
        "//src/internal/base:endpoint",
        "//src/internal/socket:acceptor",
        "//src/internal/socket:socket_tcp",
        "//src/util",
        "@abseil-cpp//absl/base:core_headers",
        "@abseil-cpp//absl/container:flat_hash_map",
        "@abseil-cpp//absl/log",
        "@abseil-cpp//absl/log:check",
        "@abseil-cpp//absl/random",
        "@abseil-cpp//absl/status",
        "@abseil-cpp//absl/status:statusor",
        "@abseil-cpp//absl/strings:str_format",
        "@abseil-cpp//absl/synchronization",
    ],
)

cc_library(
    name = "transfer",
    srcs = ["transfer.cc"],
    hdrs = ["transfer.h"],
    deps = [
        "//src/api:transport_types",
        "//src/internal:assumptions",
        "//src/internal/base:types",
        "//src/internal/buffer:buffer_tracker",
        "//src/internal/channel",
        "//src/internal/chunk",
        "//src/internal/chunk:chunk_flatbuf",
        "//src/internal/chunk:tracker",
        "//src/util:macro",
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
        "//src/internal/chunk",
        "//src/util",
        "@abseil-cpp//absl/log",
        "@abseil-cpp//absl/log:check",
        "@abseil-cpp//absl/random",
        "@abseil-cpp//absl/strings",
        "@googletest//:gtest_main",
    ],
)
