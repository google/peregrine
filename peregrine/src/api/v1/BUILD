# Public API, version 1.

load("@rules_cc//cc:cc_library.bzl", "cc_library")
load("@rules_cc//cc:cc_test.bzl", "cc_test")

package(default_visibility = ["//visibility:public"])

cc_library(
    name = "socket_util",
    hdrs = ["socket_util.h"],
    deps = [
        "//peregrine/src/internal/base:types",
        "//peregrine/src/internal/socket:socket_util",
        "//peregrine/src/internal/socket:tcp_socket_util",
        "//peregrine/src/internal/util",
        "@abseil-cpp//absl/base:core_headers",
        "@abseil-cpp//absl/log:check",
        "@abseil-cpp//absl/status",
        "@abseil-cpp//absl/strings",
        "@abseil-cpp//absl/types:span",
    ],
)

cc_test(
    name = "socket_util_test",
    srcs = ["socket_util_test.cc"],
    shard_count = 16,
    deps = [
        ":socket_util",
        "//peregrine/src/api:transport_types",
        "//peregrine/src/internal/base:endpoint",
        "//peregrine/src/internal/base:types",
        "//peregrine/src/internal/socket:socket_tcp",
        "//peregrine/src/internal/util:test_iov",
        "//peregrine/src/internal/util:test_util",
        "//peregrine/src/util",
        "//peregrine/src/util:thread",
        "@abseil-cpp//absl/log:check",
        "@abseil-cpp//absl/status",
        "@abseil-cpp//absl/status:status_matchers",
        "@abseil-cpp//absl/strings",
        "@abseil-cpp//absl/strings:str_format",
        "@abseil-cpp//absl/synchronization",
        "@abseil-cpp//absl/types:span",
        "@googletest//:gtest_main",
    ],
)

cc_library(
    name = "rate_limiter",
    hdrs = ["rate_limiter.h"],
    deps = [
        "//peregrine/src/internal/lib:rate_limiter",
    ],
)
