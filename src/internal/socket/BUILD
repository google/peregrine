load("@rules_cc//cc:cc_library.bzl", "cc_library")
load("@rules_cc//cc:cc_test.bzl", "cc_test")

package(default_visibility = ["//src/internal:internal_impl"])

cc_library(
    name = "ip_util",
    srcs = ["ip_util.cc"],
    hdrs = ["ip_util.h"],
    deps = [
        "//src/internal/base:types",
        "@abseil-cpp//absl/log",
        "@abseil-cpp//absl/log:check",
        "@abseil-cpp//absl/status",
        "@abseil-cpp//absl/status:statusor",
        "@abseil-cpp//absl/strings",
        "@abseil-cpp//absl/strings:str_format",
    ],
)

cc_test(
    name = "ip_util_test",
    srcs = ["ip_util_test.cc"],
    deps = [
        ":ip_util",
        "//src/internal/base:types",
        "//src/internal/util:test_util",
        "@abseil-cpp//absl/status",
        "@googletest//:gtest_main",
    ],
)

cc_library(
    name = "socket_util",
    srcs = ["socket_util.cc"],
    hdrs = ["socket_util.h"],
    deps = [
        ":ip_util",
        "@abseil-cpp//absl/base:core_headers",
        "@abseil-cpp//absl/log",
        "@abseil-cpp//absl/log:check",
        "@abseil-cpp//absl/status",
        "@abseil-cpp//absl/status:statusor",
        "@abseil-cpp//absl/strings",
        "@abseil-cpp//absl/strings:str_format",
    ],
)

cc_test(
    name = "socket_util_test",
    srcs = ["socket_util_test.cc"],
    deps = [
        ":socket_util",
        "@abseil-cpp//absl/log",
        "@googletest//:gtest_main",
    ],
)

cc_library(
    name = "socket_base",
    hdrs = ["socket_base.h"],
    deps = [
        ":socket_util",
        "@abseil-cpp//absl/log:check",
    ],
)

cc_library(
    name = "socket_tcp",
    srcs = ["socket_tcp.cc"],
    hdrs = ["socket_tcp.h"],
    deps = [
        ":ip_util",
        ":socket_base",
        ":socket_util",
        "//src/api:types",
        "//src/internal/base:types",
        "@abseil-cpp//absl/base:core_headers",
        "@abseil-cpp//absl/log",
        "@abseil-cpp//absl/log:check",
        "@abseil-cpp//absl/memory",
        "@abseil-cpp//absl/status",
        "@abseil-cpp//absl/status:statusor",
        "@abseil-cpp//absl/strings",
        "@abseil-cpp//absl/strings:str_format",
    ],
)

cc_test(
    name = "socket_tcp_test",
    srcs = ["socket_tcp_test.cc"],
    deps = [
        ":ip_util",
        ":socket_tcp",
        "//src/api:types",
        "//src/internal/base:types",
        "//src/internal/util:test_util",
        "@abseil-cpp//absl/log:check",
        "@abseil-cpp//absl/synchronization",
        "@googletest//:gtest_main",
    ],
)

cc_library(
    name = "socket_udp",
    srcs = ["socket_udp.cc"],
    hdrs = ["socket_udp.h"],
    deps = [
        ":ip_util",
        ":socket_base",
        ":socket_util",
        "//src/api:types",
        "//src/internal/base:types",
        "//src/internal/util",
        "@abseil-cpp//absl/base:core_headers",
        "@abseil-cpp//absl/log",
        "@abseil-cpp//absl/log:check",
        "@abseil-cpp//absl/memory",
        "@abseil-cpp//absl/status",
        "@abseil-cpp//absl/status:statusor",
        "@abseil-cpp//absl/strings",
        "@abseil-cpp//absl/strings:str_format",
    ],
)

cc_test(
    name = "socket_udp_test",
    srcs = ["socket_udp_test.cc"],
    deps = [
        ":ip_util",
        ":socket_udp",
        "//src/api:types",
        "//src/internal/base:types",
        "//src/internal/util:test_util",
        "@abseil-cpp//absl/log:check",
        "@abseil-cpp//absl/synchronization",
        "@googletest//:gtest_main",
    ],
)
