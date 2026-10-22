# Public API, version 1.

load("@rules_cc//cc:cc_library.bzl", "cc_library")
load("@rules_cc//cc:cc_test.bzl", "cc_test")

package(default_visibility = ["//visibility:public"])

cc_library(
    name = "ip_util",
    srcs = ["ip_util.cc"],
    hdrs = ["ip_util.h"],
    deps = [
        "//peregrine/src/internal/base:types",
        "//peregrine/src/internal/socket:socket_util",
        "//peregrine/src/util:envvar",
        "@abseil-cpp//absl/base:core_headers",
        "@abseil-cpp//absl/base:no_destructor",
        "@abseil-cpp//absl/cleanup",
        "@abseil-cpp//absl/container:flat_hash_map",
        "@abseil-cpp//absl/log",
        "@abseil-cpp//absl/status",
        "@abseil-cpp//absl/status:status_macros",
        "@abseil-cpp//absl/status:statusor",
        "@abseil-cpp//absl/strings",
        "@abseil-cpp//absl/synchronization",
    ],
)

cc_test(
    name = "ip_util_test",
    srcs = ["ip_util_test.cc"],
    deps = [
        ":ip_util",
        "//peregrine/src/internal/base:endpoint",
        "//peregrine/src/internal/base:types",
        "//peregrine/src/internal/socket:socket_udp",
        "//peregrine/src/internal/util:test_util",
        "@abseil-cpp//absl/cleanup",
        "@abseil-cpp//absl/log:check",
        "@abseil-cpp//absl/status",
        "@abseil-cpp//absl/status:status_matchers",
        "@abseil-cpp//absl/strings",
        "@googletest//:gtest_main",
    ],
)

cc_library(
    name = "conn_util",
    srcs = ["conn_util.cc"],
    hdrs = ["conn_util.h"],
    deps = [
        ":ip_util",
        "@abseil-cpp//absl/cleanup",
        "@abseil-cpp//absl/log",
        "@abseil-cpp//absl/random",
        "@abseil-cpp//absl/random:distributions",
        "@abseil-cpp//absl/status",
        "@abseil-cpp//absl/status:status_macros",
        "@abseil-cpp//absl/status:statusor",
        "@abseil-cpp//absl/strings",
        "@abseil-cpp//absl/time",
    ],
)

cc_test(
    name = "conn_util_test",
    srcs = ["conn_util_test.cc"],
    deps = [
        ":conn_util",
        "@abseil-cpp//absl/cleanup",
        "@abseil-cpp//absl/log:check",
        "@abseil-cpp//absl/status",
        "@abseil-cpp//absl/status:status_matchers",
        "@abseil-cpp//absl/strings",
        "@abseil-cpp//absl/time",
        "@googletest//:gtest_main",
    ],
)

cc_library(
    name = "histogram",
    hdrs = ["histogram.h"],
    deps = [
        "//peregrine/src/internal/lib:histogram",
    ],
)

cc_test(
    name = "histogram_test",
    srcs = ["histogram_test.cc"],
    deps = [
        ":histogram",
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

cc_test(
    name = "rate_limiter_test",
    srcs = ["rate_limiter_test.cc"],
    deps = [
        ":rate_limiter",
        "@abseil-cpp//absl/time",
        "@googletest//:gtest_main",
    ],
)

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
