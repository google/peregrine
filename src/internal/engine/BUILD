load("@rules_cc//cc:cc_library.bzl", "cc_library")
load("@rules_cc//cc:cc_test.bzl", "cc_test")

package(default_visibility = ["//src/internal:internal_impl"])

cc_library(
    name = "engine",
    srcs = ["engine.cc"],
    hdrs = ["engine.h"],
    deps = [
        ":worker",
        "//src/api:transport_types",
        "//src/internal:assumptions",
        "//src/internal/base:endpoint",
        "//src/internal/base:types",
        "//src/internal/channel",
        "//src/internal/channel:channel_util",
        "//src/internal/chunk",
        "//src/internal/request:request_tracker",
        "//src/internal/socket:acceptor",
        "//src/internal/socket:connector",
        "//src/internal/socket:socket_tcp",
        "//src/util",
        "@abseil-cpp//absl/base:core_headers",
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
        "//src/internal/channel",
        "//src/internal/chunk",
        "//src/internal/chunk:chunk_flatbuf",
        "//src/internal/chunk:chunk_tracker",
        "//src/internal/request:request_tracker",
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
        "//src/internal/request:request_tracker",
        "//src/util",
        "@abseil-cpp//absl/log",
        "@abseil-cpp//absl/log:check",
        "@abseil-cpp//absl/random",
        "@googletest//:gtest_main",
    ],
)

cc_library(
    name = "worker",
    srcs = ["worker.cc"],
    hdrs = ["worker.h"],
    deps = [
        ":transfer",
        "//src/api:transport_types",
        "//src/internal/base:endpoint",
        "//src/internal/channel",
        "//src/internal/chunk",
        "//src/internal/request:request_tracker",
        "//src/util:macro",
        "@abseil-cpp//absl/base:core_headers",
        "@abseil-cpp//absl/log",
        "@abseil-cpp//absl/log:check",
        "@abseil-cpp//absl/synchronization",
    ],
)

cc_test(
    name = "worker_test",
    srcs = ["worker_test.cc"],
    deps = [
        ":worker",
        "//src/api:transport_types",
        "//src/internal:assumptions",
        "//src/internal/base:endpoint",
        "//src/internal/base:types",
        "//src/internal/channel",
        "//src/internal/channel:channel_test_util",
        "//src/internal/chunk",
        "//src/internal/request:request_tracker",
        "//src/util",
        "@abseil-cpp//absl/log:check",
        "@abseil-cpp//absl/random",
        "@abseil-cpp//absl/synchronization",
        "@abseil-cpp//absl/time",
        "@googletest//:gtest_main",
    ],
)
