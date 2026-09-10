load("@rules_cc//cc:cc_library.bzl", "cc_library")
load("@rules_cc//cc:cc_test.bzl", "cc_test")

package(default_visibility = ["//src/internal:internal_impl"])

cc_library(
    name = "acceptor",
    srcs = ["acceptor.cc"],
    hdrs = ["acceptor.h"],
    deps = [
        ":socket_tcp",
        ":socket_util",
        "//src/internal:assumptions",
        "//src/internal/base:endpoint",
        "//src/internal/base:hostinfo",
        "//src/internal/base:types",
        "//src/internal/event:poller",
        "//src/internal/socket/psp",
        "//src/internal/socket/psp:psp_util",
        "//src/util:nic",
        "@abseil-cpp//absl/base:core_headers",
        "@abseil-cpp//absl/container:flat_hash_map",
        "@abseil-cpp//absl/functional:any_invocable",
        "@abseil-cpp//absl/log",
        "@abseil-cpp//absl/log:check",
        "@abseil-cpp//absl/memory",
        "@abseil-cpp//absl/status",
        "@abseil-cpp//absl/status:statusor",
        "@abseil-cpp//absl/strings",
        "@abseil-cpp//absl/synchronization",
        "@abseil-cpp//absl/types:span",
    ],
)

cc_test(
    name = "acceptor_test",
    srcs = ["acceptor_test.cc"],
    deps = [
        ":acceptor",
        ":socket_tcp",
        "//src/internal/base:endpoint",
        "//src/internal/base:hostinfo",
        "//src/internal/socket/psp",
        "//src/internal/socket/psp:psp_mock",
        "//src/internal/socket/psp:psp_util",
        "//src/internal/util:test_util",
        "@abseil-cpp//absl/log:check",
        "@abseil-cpp//absl/status",
        "@abseil-cpp//absl/status:status_matchers",
        "@abseil-cpp//absl/time",
        "@googletest//:gtest_main",
    ],
)

cc_library(
    name = "connector",
    srcs = ["connector.cc"],
    hdrs = ["connector.h"],
    deps = [
        ":socket_tcp",
        "//src/internal/base:endpoint",
        "//src/internal/base:types",
        "//src/internal/socket/psp",
        "//src/internal/socket/psp:psp_util",
        "@abseil-cpp//absl/base:core_headers",
        "@abseil-cpp//absl/functional:any_invocable",
        "@abseil-cpp//absl/log",
        "@abseil-cpp//absl/log:check",
        "@abseil-cpp//absl/status:statusor",
    ],
)

cc_test(
    name = "connector_test",
    srcs = ["connector_test.cc"],
    deps = [
        ":acceptor",
        ":connector",
        ":socket_tcp",
        "//src/internal/base:endpoint",
        "//src/internal/base:hostinfo",
        "//src/internal/socket/psp",
        "//src/internal/socket/psp:psp_mock",
        "//src/internal/socket/psp:psp_util",
        "//src/internal/util:test_util",
        "@abseil-cpp//absl/log",
        "@abseil-cpp//absl/log:check",
        "@abseil-cpp//absl/time",
        "@googletest//:gtest_main",
    ],
)

cc_library(
    name = "socket_util",
    srcs = ["socket_util.cc"],
    hdrs = ["socket_util.h"],
    deps = [
        "//src/internal/base:endpoint",
        "//src/internal/base:types",
        "@abseil-cpp//absl/base:core_headers",
        "@abseil-cpp//absl/log",
        "@abseil-cpp//absl/log:check",
        "@abseil-cpp//absl/strings",
        "@abseil-cpp//absl/strings:str_format",
    ],
)

cc_test(
    name = "socket_util_test",
    srcs = ["socket_util_test.cc"],
    deps = [
        ":socket_util",
        "//src/internal/base:types",
        "@abseil-cpp//absl/log",
        "@googletest//:gtest_main",
    ],
)

cc_library(
    name = "socket_base",
    srcs = ["socket_base.cc"],
    hdrs = ["socket_base.h"],
    deps = [
        ":socket_util",
        "//src/internal/base:endpoint",
        "//src/internal/base:types",
        "//src/util:macro",
        "@abseil-cpp//absl/log:check",
    ],
)

cc_library(
    name = "socket_tcp",
    srcs = ["socket_tcp.cc"],
    hdrs = ["socket_tcp.h"],
    deps = [
        ":socket_base",
        ":socket_util",
        "//src/api:transport_types",
        "//src/internal/base:endpoint",
        "//src/internal/base:types",
        "//src/internal/util",
        "@abseil-cpp//absl/base:core_headers",
        "@abseil-cpp//absl/log",
        "@abseil-cpp//absl/log:check",
        "@abseil-cpp//absl/memory",
        "@abseil-cpp//absl/strings",
        "@abseil-cpp//absl/types:span",
    ],
)

cc_test(
    name = "socket_tcp_test",
    srcs = ["socket_tcp_test.cc"],
    deps = [
        ":socket_tcp",
        "//src/api:transport_types",
        "//src/internal/base:endpoint",
        "//src/internal/base:types",
        "//src/internal/util:test_util",
        "//src/util",
        "@abseil-cpp//absl/log:check",
        "@abseil-cpp//absl/synchronization",
        "@abseil-cpp//absl/types:span",
        "@googletest//:gtest_main",
    ],
)

cc_library(
    name = "socket_udp",
    srcs = ["socket_udp.cc"],
    hdrs = ["socket_udp.h"],
    deps = [
        ":socket_base",
        ":socket_util",
        "//src/api:transport_types",
        "//src/internal/base:endpoint",
        "//src/internal/base:types",
        "//src/internal/util",
        "@abseil-cpp//absl/base:core_headers",
        "@abseil-cpp//absl/log",
        "@abseil-cpp//absl/log:check",
        "@abseil-cpp//absl/memory",
        "@abseil-cpp//absl/strings",
        "@abseil-cpp//absl/types:span",
    ],
)

cc_test(
    name = "socket_udp_test",
    srcs = ["socket_udp_test.cc"],
    deps = [
        ":socket_udp",
        "//src/api:transport_types",
        "//src/internal/base:endpoint",
        "//src/internal/util:test_util",
        "@abseil-cpp//absl/log:check",
        "@abseil-cpp//absl/synchronization",
        "@googletest//:gtest_main",
    ],
)

cc_library(
    name = "socket_test_util",
    testonly = True,
    srcs = ["socket_test_util.cc"],
    hdrs = ["socket_test_util.h"],
    deps = [
        ":acceptor",
        ":connector",
        ":socket_tcp",
        ":socket_udp",
        "//src/internal/base:endpoint",
        "//src/internal/base:hostinfo",
        "//src/internal/util:test_util",
        "@abseil-cpp//absl/log:check",
        "@abseil-cpp//absl/synchronization",
    ],
)

cc_library(
    name = "tcp_socket_util",
    srcs = ["tcp_socket_util.cc"],
    hdrs = ["tcp_socket_util.h"],
    deps = [
        ":socket_util",
        "//src/api:transport_types",
        "//src/internal/base:types",
        "//src/internal/util",
        "@abseil-cpp//absl/base:core_headers",
        "@abseil-cpp//absl/log",
        "@abseil-cpp//absl/log:check",
        "@abseil-cpp//absl/status",
        "@abseil-cpp//absl/strings:str_format",
        "@abseil-cpp//absl/types:span",
    ],
)

cc_test(
    name = "tcp_socket_util_test",
    srcs = ["tcp_socket_util_test.cc"],
    deps = [
        ":socket_tcp",
        ":tcp_socket_util",
        "//src/api:transport_types",
        "//src/internal/base:endpoint",
        "//src/internal/base:types",
        "//src/internal/util:test_util",
        "//src/util",
        "@abseil-cpp//absl/log:check",
        "@abseil-cpp//absl/synchronization",
        "@abseil-cpp//absl/types:span",
        "@googletest//:gtest_main",
    ],
)
